//
// Created by Bolke de Bruin on 06/08/2025.
//
#include "config.h"
#include "ESPAsyncWebServer.h"
#include "WifiManager.h"
#include "components/ConfigManager.h"
#include "json/StreamWrappers.h"
#include "json/WindowOutputStream.h"
#include "Update.h"
#include "OtaManager.h"
#include "scripting/BytecodeWriter.h"
#include "scripting/BytecodeRegistry.h"
#include "scripting/WebSocketREPL.h"
#include "scripting/ScriptEngine.h"
#include "drivers/DeviceInfo.h"
#include "drivers/LvglDisplay.h"
#include "S3Server.h"
#include "Auth.h"
#include <ArduinoJson.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_system.h>
#include <LittleFS.h>
#include <new>

static auto TAG = "webserver";

// External reference to global ConfigManager
extern freebutton::ConfigManager configManager;

AsyncWebServer server(80);

// Streams the config without buffering it. AsyncResponseStream grows one
// buffer a byte at a time (the JSON writer Puts single chars), which needs
// old + new block per step: on a fragmented heap it stopped at ~3.8 KB,
// truncating the JSON and draining the heap to a few hundred bytes. Instead,
// measure once, then re-run the writer per TCP chunk and keep only that
// chunk's window (WindowOutputStream). Peak RAM is the server's chunk buffer.
void handleConfig(AsyncWebServerRequest *request) {
    WindowOutputStream counter;
    configManager.write(counter);

    AsyncWebServerResponse *response = request->beginResponse(
        "application/json", counter.total(),
        [](uint8_t *buffer, const size_t maxLen, const size_t index) -> size_t {
            WindowOutputStream window(buffer, index, maxLen);
            configManager.write(window);
            return window.copied();
        });
    request->send(response);
}

// OTA Update Functionality

static constexpr std::string_view upgradePage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>FreeButton Update</title>
    <style>
        body {
            font-family: Arial, sans-serif;
            text-align: center;
            margin-top: 50px;
        }
        h2 {
            color: #333;
        }
        form {
            margin-bottom: 20px;
        }
        #progress {
            width: 100%;
            background-color: #f3f3f3;
        }
        #bar {
            width: 0%;
            height: 30px;
            background-color: #4caf50;
        }
    </style>
</head>
<body>
    <h2>FreeButton Update</h2>
    <form id="uploadForm" method="POST" action="/upload" enctype="multipart/form-data">
        <select id="updateType" name="updateType">
            <option value="firmware">Firmware Update</option>
            <option value="filesystem">Filesystem Update</option>
        </select>
        <input type="file" name="updateFile" required>
        <input type="submit" value="Upload Update" id="uploadButton">
    </form>
    <div id="progress">
        <div id="bar"></div>
    </div>
    <button id="otaButton" onclick="triggerOtaUpdate()" disabled>Trigger OTA Update</button>

    <script>
        const form = document.getElementById('uploadForm');
        const otaButton = document.getElementById('otaButton');
        const progress = document.getElementById('progress');
        const bar = document.getElementById('bar');
        const uploadButton = document.getElementById('uploadButton');

        form.onsubmit = function(event) {
            event.preventDefault();
            const formData = new FormData(form);
            const xhr = new XMLHttpRequest();
            xhr.open('POST', '/upload', true);

            xhr.upload.onprogress = function(event) {
                if (event.lengthComputable) {
                    const percentComplete = (event.loaded / event.total) * 100;
                    bar.style.width = percentComplete + '%';
                }
            };

            xhr.onload = function() {
                uploadButton.disabled = false;
                if (xhr.status == 200) {
                    alert(xhr.responseText);
                    if (xhr.responseText.includes('Upload complete')) {
                        otaButton.disabled = false;
                    }
                } else {
                    alert('Upload failed');
                }
            };

            xhr.onerror = function() {
                uploadButton.disabled = false;
                alert('Network error');
            };

            uploadButton.disabled = true;
            xhr.send(formData);
        };

        function triggerOtaUpdate() {
            fetch('/ota_update', {
                method: 'POST'
            }).then(response => response.text()).then(data => {
                alert(data);
            }).catch(error => {
                console.error('Error:', error);
            });
        }
    </script>
</body>
</html>
)rawliteral";

void handleUpgradePage(AsyncWebServerRequest *request) {
    const std::string content{upgradePage};
    request->send(200, "text/html", content.c_str());
}

// OTA upload state (async_tcp task only).
static const char* s_uploadError = nullptr;  // refused before anything was written
static bool s_fsUnmounted = false;           // LittleFS unmounted for a filesystem OTA

// Function to handle file uploads
// updateType=firmware: the app image goes to the next OTA slot.
// updateType=filesystem: a complete LittleFS image (pio buildfs /
// tools/scripts/ota_fs.py) replaces the data partition. It is checked first
// (LittleFS magic), refused while an S3 transfer has a file open, and only
// then LittleFS is unmounted, so nothing reads the partition while it is
// rewritten (S3 answers 503, static files 404 meanwhile). Apply with
// /ota_update (reboot). A failure after writing started leaves the
// filesystem unusable until a successful upload; the flash-resident
// /upgrade page and this endpoint keep working.
void onFileUpload(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
    static std::string updateType;
    if (!index) {
        ESP_LOGI(TAG, "Upload start for: %s", filename.c_str());
        s_uploadError = nullptr;

        // Get update type from request
        updateType = std::string(request->arg("updateType").c_str());
        if (updateType == "firmware") {
            ESP_LOGI(TAG, "Starting firmware update");
            if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                Update.printError(Serial);
                ESP_LOGE(TAG, "Update.begin() failed");
            }
        } else if (updateType == "filesystem") {
            // LittleFS superblock: the "littlefs" magic at byte 8 of block 0.
            if (len < 16 || memcmp(data + 8, "littlefs", 8) != 0) {
                s_uploadError = "not a LittleFS image";
            } else if (!s3_idle()) {
                s_uploadError = "storage busy (S3 transfer), retry";
            } else {
                ESP_LOGW(TAG, "Starting filesystem update: unmounting LittleFS");
                LittleFS.end();
                s_fsUnmounted = true;
                if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_SPIFFS)) {
                    Update.printError(Serial);
                    ESP_LOGE(TAG, "Update.begin() failed");
                }
            }
        } else {
            s_uploadError = "updateType must be firmware or filesystem (and come before the file)";
        }
        if (s_uploadError) {
            ESP_LOGE(TAG, "Upload refused: %s", s_uploadError);
        }
    }

    if (s_uploadError) {
        return;  // refused: drain the body, write nothing
    }

    if (Update.write(data, len) != len) {
        Update.printError(Serial);
        ESP_LOGE(TAG, "Update.write() failed");
    }

    if (final) {
        if (Update.end(true)) {
            ESP_LOGI(TAG, "Update success: %u bytes", index + len);
        } else {
            Update.printError(Serial);
            ESP_LOGE(TAG, "Update.end() failed");
        }
    }
}

void handleFirmwareUpload(AsyncWebServerRequest *request) {
    if (s_uploadError) {
        request->send(400, "text/plain", s_uploadError);
    } else if (Update.hasError()) {
        request->send(500, "text/plain",
                      s_fsUnmounted ? "Upload failed; the filesystem must be uploaded again." : "Upload failed.");
    } else {
        request->send(200, "text/plain", "Upload complete. Click 'Trigger OTA Update' to apply.");
    }
}

void handleOtaUpdate(AsyncWebServerRequest *request) {
    if (Update.hasError()) {
        request->send(500, "text/plain", "Update failed.");
    } else {
        request->send(200, "text/plain", "Update successful. Rebooting...");
        delay(1000);
        ESP.restart();
    }
}

void handleRemoteFirmwareUpdate(AsyncWebServerRequest *request) {
    ESP_LOGI(TAG, "Remote firmware update requested");

    int firmware_id = 0;
    if (request->hasParam("id")) {
        firmware_id = std::stoi(request->getParam("id")->value().c_str());
        ESP_LOGI(TAG, "Firmware ID: %d", firmware_id);
    }

    OtaResult result = ota_start_remote_update(firmware_id);
    if (result == OtaResult::SUCCESS) {
        request->send(200, "text/plain", "Remote firmware update initiated...");
    } else {
        request->send(500, "text/plain", "Failed to start OTA update");
    }
}

// Bytecode Upload Functionality
// Per-request state in request->_tempObject (malloc'd: the server free()s it).
// The writer (~8.5 KB: two 4 KB work buffers) is allocated with malloc +
// placement new so a full heap answers 500 instead of aborting, and is
// destroyed from onDisconnect if the client goes away mid-upload (otherwise
// its relocation context and the registry's upload reservation would leak).
struct BytecodeUpload {
    BytecodeWriter* writer;
    esp_err_t error;
    bool restart;  // the boot script was stored: the engine restarts with it
};

static void destroyWriter(BytecodeUpload* upload) {
    if (upload && upload->writer) {
        upload->writer->~BytecodeWriter();
        free(upload->writer);
        upload->writer = nullptr;
    }
}

void onBytecodeUpload(AsyncWebServerRequest *request, const String& filename,
                      size_t index, uint8_t *data, size_t len, bool final) {
    auto* upload = static_cast<BytecodeUpload*>(request->_tempObject);

    if (index == 0 && !upload) {
        size_t content_length = request->contentLength();
        ESP_LOGI(TAG, "Bytecode upload start: %s (%zu bytes)", filename.c_str(), content_length);

        upload = static_cast<BytecodeUpload*>(malloc(sizeof(BytecodeUpload)));
        if (!upload) return;  // handleBytecodeUpload answers 500
        upload->writer = nullptr;
        upload->error = ESP_OK;
        upload->restart = false;
        request->_tempObject = upload;
        request->onDisconnect([request]() {
            destroyWriter(static_cast<BytecodeUpload*>(request->_tempObject));
        });

        void* mem = malloc(sizeof(BytecodeWriter));
        if (!mem) {
            ESP_LOGE(TAG, "No heap for the bytecode writer");
            upload->error = ESP_ERR_NO_MEM;
            return;
        }
        upload->writer = new (mem) BytecodeWriter();

        // Initialize with total size for upfront flash erase
        const esp_err_t err = upload->writer->beginWrite(filename.c_str(), content_length);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "beginWrite failed: 0x%x", err);
            upload->error = err;
            destroyWriter(upload);
            return;
        }
    }

    if (!upload || !upload->writer) {
        return;
    }

    // Write chunk directly to flash with inline relocation
    const esp_err_t err = upload->writer->write(data, len, final);

    if (final) {
        ESP_LOGI(TAG, "Bytecode upload complete: %s", filename.c_str());
        upload->error = err;
        destroyWriter(upload);
        // The boot script (main.jsc, or script.jsc while there is no
        // main.jsc) is stored: ask the loop task to restart the engine with
        // it. Only a request: this is the async_tcp task. Asked here and not
        // in handleBytecodeUpload, so it also happens when the client goes
        // away before the answer. Another name changes nothing that runs.
        if (err == ESP_OK && BytecodeRegistry::instance().isBootScript(filename.c_str())) {
            upload->restart = Scripting::ScriptEngine::getInstance().requestRestart();
        }
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "Upload failed during chunk write: 0x%x", err);
        upload->error = err;
        destroyWriter(upload);
    }
}

void handleBytecodeUpload(AsyncWebServerRequest *request) {
    auto* upload = static_cast<BytecodeUpload*>(request->_tempObject);

    if (!upload) {
        request->send(500, "application/json", R"({"status":"error","message":"Upload failed"})");
        return;
    }
    // A request without a file part never reached the final chunk.
    const esp_err_t err = upload->writer ? ESP_FAIL : upload->error;
    destroyWriter(upload);

    if (err == ESP_OK) {
        // "restart": the stored script is being started now (no reboot
        // needed). Firmware before the script restart has no such field.
        request->send(200, "application/json",
                      upload->restart ? R"({"status":"success","restart":true})"
                                      : R"({"status":"success","restart":false})");
    } else {
        char body[96];
        snprintf(body, sizeof body, R"({"status":"error","message":"Write failed","code":%d})", err);
        request->send(200, "application/json", body);  // status in the body, as before
    }
}

// GET /api/bytecode/list: the registry's active entries, rendered twice into
// the response window (count, then per TCP chunk) like /config.
template <class S>
static void writeBytecodeList(S& out) {
    auto put = [&out](const char* s) { while (*s) out.Put(*s++); };
    put("{\"entries\":[");
    bool first = true;
    BytecodeRegistry::instance().forEachActive([&](const BytecodeEntry& entry) {
        if (!first) out.Put(',');
        first = false;
        put("{\"name\":\"");
        for (const char* c = entry.name; *c; ++c) {
            if (*c == '"' || *c == '\\') out.Put('\\');
            if (static_cast<unsigned char>(*c) >= 0x20) out.Put(*c);
        }
        char num[48];
        // stale: uploaded for another firmware build; it is not run (redeploy).
        snprintf(num, sizeof num, "\",\"size\":%lu,\"stale\":%s}", static_cast<unsigned long>(entry.size),
                 entry.app_id == bytecode_current_app_id() ? "false" : "true");
        put(num);
    });
    put("]}");
}

static void handleBytecodeList(AsyncWebServerRequest *request) {
    WindowOutputStream counter;
    writeBytecodeList(counter);
    AsyncWebServerResponse *response = request->beginResponse(
        "application/json", counter.total(),
        [](uint8_t *buffer, const size_t maxLen, const size_t index) -> size_t {
            WindowOutputStream window(buffer, index, maxLen);
            writeBytecodeList(window);
            return window.copied();
        });
    request->send(response);
}

// POST /api/reboot: answer first, restart 500 ms later from an esp_timer
// (never delay() on the async_tcp task).
static void handleReboot(AsyncWebServerRequest *request) {
    static esp_timer_handle_t timer = nullptr;
    if (!timer) {
        esp_timer_create_args_t args = {};
        args.callback = [](void*) { esp_restart(); };
        args.name = "reboot";
        if (esp_timer_create(&args, &timer) != ESP_OK) {
            timer = nullptr;
            request->send(500, "application/json", R"({"status":"error","message":"timer"})");
            return;
        }
    }
    ESP_LOGI(TAG, "Reboot requested over HTTP");
    esp_timer_start_once(timer, 500 * 1000);  // already running: keep that one
    request->send(200, "application/json", R"({"status":"rebooting"})");
}

void handleBytecodeDownload(AsyncWebServerRequest *request) {
    // Download the temp file created by BytecodeWriter
    // Default name is "script" but can be overridden
    String filename = "script.jsc";
    if (request->hasParam("name")) {
        filename = request->getParam("name")->value();
    }

    String temp_path = String("/tmp_reloc_") + filename;

    // Check if file exists
    if (!LittleFS.exists(temp_path)) {
        request->send(404, "text/plain", "Temp file not found. Upload bytecode first.");
        return;
    }

    // Send file directly from LittleFS
    request->send(LittleFS, temp_path, "application/octet-stream", true);
}

void webserver_start() {
    // The web IDE is now served locally from LittleFS /www/ (same-origin), so the
    // wildcard CORS + Private-Network-Access DefaultHeaders that the former
    // externally-hosted (cross-origin) IDE required are no longer needed and
    // have been removed. Cross-origin (dev) clients get CORS headers from the
    // auth middleware (fbauth::installGate).
    // The HTTP_OPTIONS -> 200 preflight guard is kept in onNotFound for any
    // remaining tooling that issues a CORS preflight against the API.

    // SigV4 authentication (Auth.h, docs/AUTH.md). The gate must be the FIRST
    // handler: it takes over a protected request whose signature fails at the
    // end of its headers, before any body byte reaches a handler. Signature
    // required: POST /api/reboot, /upload, /ota_update, /updatefirmware,
    // /resetWifi, /api/bytecode/upload; GET /config; the /ws/repl upgrade.
    // Anonymous: /api/version, /api/device, /api/bytecode/list,
    // /api/bytecode/next_address, the /upgrade page (its unsigned form
    // post now gets 403), static www, captive portal, OPTIONS.
    fbauth::init();
    fbauth::installGate(server);

    // SPA fallback: any GET that is not an API/WebSocket route and does not map
    // to a static file serves the gzipped app shell so client-side routing works.
    server.onNotFound([](AsyncWebServerRequest *request){
        if (request->method() == HTTP_OPTIONS) {
            request->send(200);
            return;
        }
        // Captive portal: OS connectivity probes (/hotspot-detect.html,
        // /generate_204, /connecttest.txt, ...) resolve to us via the portal's
        // DNS. Redirect every unknown GET to the WiFi setup page; without this
        // the SPA fallback below answered them with the web IDE.
        if (is_captive_portal_active() && request->method() == HTTP_GET) {
            request->redirect("http://192.168.4.1/");
            return;
        }
        const String& url = request->url();
        if (request->method() == HTTP_GET &&
            !url.startsWith("/api") && !url.startsWith("/ws")) {
            static constexpr const char* SPA_SHELL = "/www/index.html.gz";
            if (LittleFS.exists(SPA_SHELL)) {
                AsyncWebServerResponse *response =
                    request->beginResponse(LittleFS, SPA_SHELL, "text/html");
                response->addHeader("Content-Encoding", "gzip");
                request->send(response);
                return;
            }
        }
        request->send(404);
    });

    // The API routes are registered BEFORE the S3 handler: a signed request
    // (Authorization: AWS4-HMAC-SHA256, x-amz-* headers) looks like an S3
    // client to it, and it would answer e.g. GET /config as bucket "config".
    server.on("/config", HTTP_GET, handleConfig);

    // Firmware/build metadata (defines injected by build_info_flags.py).
    server.on("/api/version", HTTP_GET, [](AsyncWebServerRequest *request){
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION FIRMWARE
#endif
#ifndef GIT_COMMIT
#define GIT_COMMIT "unknown"
#endif
#ifndef BUILD_DIRTY
#define BUILD_DIRTY 0
#endif
        char body[256];
        int n = snprintf(body, sizeof(body),
            "{\"version\":\"%s\",\"git_commit\":\"%s\",\"dirty\":%s,\"model\":\"%s\"}",
            FIRMWARE_VERSION, GIT_COMMIT, (BUILD_DIRTY ? "true" : "false"), MODEL);
        if (n < 0 || n >= (int)sizeof(body)) {
            request->send(500, "application/json", "{\"error\":\"version overflow\"}");
            return;
        }
        request->send(200, "application/json", body);
    });

    // Live device info for the web IDE's device panel
    server.on("/api/device", HTTP_GET, [](AsyncWebServerRequest *request){
        FreeButton::DeviceInfoCollector collector;
        FreeButton::DeviceInfo info = collector.collectInfo();
        // Read-only device info; allow a cross-origin dev build (Vite talking to
        // a remote device) to read it. The IDE is served same-origin in prod, so
        // this only matters for the explicit-device-URL dev workflow.
        AsyncWebServerResponse *response =
            request->beginResponse(200, "application/json", FreeButton::toJson(info).c_str());
        response->addHeader("Access-Control-Allow-Origin", "*");
        request->send(response);
    });

    // POST only (and signed): as a GET it was a one-click WiFi wipe for any page.
    server.on("/resetWifi", HTTP_POST, [](AsyncWebServerRequest *request){
        request->send(200, "text/plain", "Resetting WiFi settings and rebooting...");
        delay(1000);
        reset_wifi_credentials();
    });

    // OTA endpoints
    server.on("/upgrade", HTTP_GET, handleUpgradePage);
    server.on("/upload", HTTP_POST, handleFirmwareUpload, onFileUpload);
    server.on("/ota_update", HTTP_POST, handleOtaUpdate);
    server.on("/updatefirmware", HTTP_POST, handleRemoteFirmwareUpdate);

    // Bytecode upload endpoints (the former /bytecode dev page was removed:
    // its unsigned upload was refused; upload from the IDE).

    server.on("/api/bytecode/upload", HTTP_POST, handleBytecodeUpload, onBytecodeUpload);
    server.on("/api/bytecode/list", HTTP_GET, handleBytecodeList);
    server.on("/api/reboot", HTTP_POST, handleReboot);
    // Debug: repaint every display once and save what is flushed to LittleFS
    // /screenshot-*.rgb565 (bucket app; tools/scripts/screenshot.py).
    // Debug: repaint every display ?n= times (default 5) and time it; GET
    // returns the newest result once, then only its "seq"
    // (tools/scripts/bench.py).
    server.on("/api/bench", HTTP_POST, [](AsyncWebServerRequest *request){
        int n = 5;
        if (request->hasParam("n")) n = request->getParam("n")->value().toInt();
        FreeButton::LvglDisplay::requestBench(n);
        request->send(202, "application/json", "{\"status\":\"requested\"}");
    });
    server.on("/api/bench", HTTP_GET, [](AsyncWebServerRequest *request){
        uint32_t seq = 0;
        std::unique_ptr<char, decltype(&free)> json(FreeButton::LvglDisplay::takeBenchResult(&seq), &free);
        if (json) {
            request->send(200, "application/json", json.get());
            return;
        }
        char none[32];
        snprintf(none, sizeof none, "{\"seq\":%lu}", static_cast<unsigned long>(seq));
        request->send(200, "application/json", none);
    });
    server.on("/api/screenshot", HTTP_POST, [](AsyncWebServerRequest *request){
        FreeButton::LvglDisplay::requestScreenshot();
        request->send(202, "application/json", "{\"status\":\"requested\"}");
    });

    // Reports where the next (small) uploaded bytecode blob would land in the
    // js_user partition and the free gap there (the device relocates uploads
    // itself; the IDE compiles position-independent bytecode).
    server.on("/api/bytecode/next_address", HTTP_GET, [](AsyncWebServerRequest *request){
        auto& registry = BytecodeRegistry::instance();
        BytecodeRegistry::Lock lock;
        const uint32_t offset = registry.getNextOffset();
        const uint32_t virtual_addr = registry.getVirtualBaseAddress() + offset;
        const size_t free_space = registry.getFreeSpace();
        // rom_base / atoms_id: where this build's stdlib ROM table is and
        // which ROM atoms it has. An IDE whose compiler has the same atoms
        // resolves them for this address (BytecodeWriter.cpp).
        char body[224];
        int n = snprintf(body, sizeof(body),
            "{\"offset\":\"0x%lx\",\"virtual_addr\":\"0x%lx\",\"free_space\":%zu,"
            "\"rom_base\":\"0x%lx\",\"atoms_id\":\"0x%lx\"}",
            (unsigned long)offset, (unsigned long)virtual_addr, free_space,
            (unsigned long)bytecode_stdlib_rom_base(),
            (unsigned long)bytecode_stdlib_atoms_id());
        if (n < 0 || n >= (int)sizeof(body)) {
            request->send(500, "application/json", "{\"error\":\"next_address overflow\"}");
            return;
        }
        request->send(200, "application/json", body);
    });

    // WebSocket REPL for JavaScript debugging; the upgrade needs a presigned
    // URL. (The former /ws-test page is gone: it could not sign the upgrade.)
    Scripting::WebSocketREPL::getInstance().init(server);

    // S3-compatible file API (buckets app, web, examples, fonts). Registered
    // after the API routes (see above) and before serveStatic so
    // "/<bucket>/..." and S3 ListBuckets on "/" reach it first (browser GETs
    // of /fonts/... still fall through to the IDE).
    s3_register(server);

    // Serve the locally-built web IDE bundle (with .gz auto-negotiation) from
    // LittleFS /www/. index.html is the default document for "/".
    server.serveStatic("/", LittleFS, "/www/").setDefaultFile("index.html");

    ESP_LOGI(TAG, "Starting webserver");
    server.begin();
}