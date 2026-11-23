//
// Created by Bolke de Bruin on 06/08/2025.
//
#if __has_include("fb_build_info.h")
#include "fb_build_info.h"   // GIT_COMMIT, BUILD_DIRTY (generated per build)
#endif
#include "config.h"
#include "ESPAsyncWebServer.h"
#include "WifiManager.h"
#include "components/ConfigManager.h"
#include "json/WindowOutputStream.h"
#include "Update.h"
#include "scripting/BytecodeWriter.h"
#include "scripting/BytecodeRegistry.h"
#include "scripting/WebSocketREPL.h"
#include "scripting/ScriptEngine.h"
#include "drivers/DeviceInfo.h"
#include "drivers/LvglDisplay.h"
#include "S3Server.h"
#include "StaticFiles.h"
#include "StreamResponse.h"
#include "Auth.h"
#include "RequestGuard.h"
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
// chunk's window (WindowOutputStream). Peak RAM is one segment-sized scratch
// block, and the response takes its turn with the downloads (StreamResponse).

// A full heap answers 503 instead of a response.
static void sendOrBusy(AsyncWebServerRequest *request, AsyncWebServerResponse *response) {
    if (response) {
        request->send(response);
    } else {
        request->send(503);
    }
}

// write is a plain function (no closure: nothing on the heap); it runs once
// to count and once per chunk, and must write the same text every time.
using JsonWriterFn = void (*)(WindowOutputStream& out);

static void sendGeneratedJson(AsyncWebServerRequest *request, JsonWriterFn write) {
    WindowOutputStream counter;
    write(counter);

    sendOrBusy(request, stream::beginGenerated(
        counter.total(), "application/json",
        [](void* context, uint8_t *buffer, const size_t maxLen, const size_t index) -> size_t {
            WindowOutputStream window(buffer, index, maxLen);
            reinterpret_cast<JsonWriterFn>(context)(window);
            return window.copied();
        }, reinterpret_cast<void*>(write)));
}

void handleConfig(AsyncWebServerRequest *request) {
    sendGeneratedJson(request, [](WindowOutputStream& out) { configManager.write(out); });
}

// Restarts the device 500 ms from now, from an esp_timer: the answer to the
// request goes out first, and nothing waits with delay() on the async_tcp
// task (the answer would not be sent before the restart). False when the
// timer cannot be made; nothing restarts then.
static bool scheduleRestart() {
    static esp_timer_handle_t timer = nullptr;
    if (!timer) {
        esp_timer_create_args_t args = {};
        args.callback = [](void*) { esp_restart(); };
        args.name = "reboot";
        if (esp_timer_create(&args, &timer) != ESP_OK) {
            timer = nullptr;
            return false;
        }
    }
    esp_timer_start_once(timer, 500 * 1000);  // already running: keep that one
    return true;
}

// OTA Update Functionality

// OTA upload state (async_tcp task only). One upload at a time: its request
// owns the updater until it disconnects.
static AsyncWebServerRequest* s_uploadOwner = nullptr;
static bool s_updaterOpen = false;           // Update.begin() succeeded, end() not yet called
static const char* s_uploadError = nullptr;  // refused before anything was written
// The signature covers the URL, not the multipart body. So the URL carries
// the SHA-256 of the file ("?sha256=<64 hex>"), and the file is only
// accepted when what arrived has that hash.
static fbauth::Sha256 s_uploadSha;
static uint8_t s_uploadWant[32];

// The hash a signed upload URL announces. False when it has none.
static bool announcedSha256(AsyncWebServerRequest *request, uint8_t out[32]) {
    const AsyncWebParameter* p = request->getParam("sha256");
    return p && fbauth::fromHex(fbauth::sv(p->value().c_str(), p->value().length()), out, 32);
}
static bool s_fsUnmounted = false;           // LittleFS unmounted for a filesystem OTA

// The updater holds a 4 KB buffer from begin() to end()/abort(), and refuses
// a new begin() while a session is open.
static void closeUpdater() {
    if (s_updaterOpen) {
        Update.abort();
        s_updaterOpen = false;
    }
}

static bool beginUpdate(int command) {
    if (Update.isRunning()) Update.abort();  // a session nobody owns any more
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, command)) {
        Update.printError(Serial);
        ESP_LOGE(TAG, "Update.begin() failed");
        s_uploadError = "the updater could not start (out of memory?)";
        return false;
    }
    s_updaterOpen = true;
    return true;
}

// Function to handle file uploads
// updateType=firmware: the app image goes to the next OTA slot.
// updateType=filesystem: a complete LittleFS image (pio buildfs /
// tools/scripts/ota_fs.py) replaces the data partition. It is checked first
// (LittleFS magic), refused while an S3 transfer has a file open, and only
// then LittleFS is unmounted, so nothing reads the partition while it is
// rewritten (S3 answers 503, static files 404 meanwhile). Apply with
// /ota_update (reboot). A failure after writing started leaves the
// filesystem unusable until a successful upload; this endpoint needs no
// filesystem and keeps working: send the image again
// (tools/scripts/ota_fs.py, or an IDE page that is still open).
void onFileUpload(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
    if (!index) {
        if (s_uploadOwner && s_uploadOwner != request) {
            return;  // another upload is being written: this one is drained and refused
        }
        ESP_LOGI(TAG, "Upload start for: %s", filename.c_str());
        s_uploadOwner = request;
        s_uploadError = nullptr;
        // A client that goes away mid-upload must not leave the updater
        // open: its buffer would stay allocated and the next upload would be
        // written into the old session.
        request->onDisconnect([request]() {
            if (s_uploadOwner != request) return;
            if (s_updaterOpen) ESP_LOGW(TAG, "Upload interrupted: update aborted");
            closeUpdater();
            s_uploadOwner = nullptr;
        });

        const String& updateType = request->arg("updateType");
        if (!announcedSha256(request, s_uploadWant)) {
            s_uploadError = "the upload URL needs ?sha256=<SHA-256 of the file>";
        } else if (updateType == "firmware") {
            ESP_LOGI(TAG, "Starting firmware update");
            beginUpdate(U_FLASH);
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
                beginUpdate(U_SPIFFS);
            }
        } else {
            s_uploadError = "updateType must be firmware or filesystem (and come before the file)";
        }
        if (s_uploadError) {
            ESP_LOGE(TAG, "Upload refused: %s", s_uploadError);
        }
        s_uploadSha.init();
    }

    if (s_uploadOwner != request || s_uploadError || !s_updaterOpen) {
        return;  // refused or failed: drain the body, write nothing
    }

    s_uploadSha.update(data, len);
    if (Update.write(data, len) != len) {
        Update.printError(Serial);
        ESP_LOGE(TAG, "Update.write() failed");
    }

    if (final) {
        uint8_t got[32];
        s_uploadSha.finish(got);
        if (!fbauth::digestEqual(got, s_uploadWant, 32)) {
            // Not what was signed: a firmware image is not made bootable. A
            // filesystem is already overwritten and must be uploaded again.
            closeUpdater();
            s_uploadError = "the file does not have the announced SHA-256";
            ESP_LOGE(TAG, "Upload refused: %s", s_uploadError);
            return;
        }
        s_updaterOpen = false;  // end() closes the session, whatever it returns
        if (Update.end(true)) {
            ESP_LOGI(TAG, "Update success: %u bytes", index + len);
        } else {
            Update.printError(Serial);
            ESP_LOGE(TAG, "Update.end() failed");
        }
    }
}

void handleFirmwareUpload(AsyncWebServerRequest *request) {
    if (s_uploadOwner != request) {
        if (s_uploadOwner) {
            request->send(409, "text/plain", "Another upload is in progress.");
        } else {
            request->send(400, "text/plain", "No file in the request.");
        }
    } else if (s_uploadError) {
        request->send(400, "text/plain", s_uploadError);
    } else if (Update.hasError()) {
        request->send(500, "text/plain",
                      s_fsUnmounted ? "Upload failed; the filesystem must be uploaded again." : "Upload failed.");
    } else {
        request->send(200, "text/plain", "Upload complete. POST /ota_update applies it.");
    }
}

void handleOtaUpdate(AsyncWebServerRequest *request) {
    if (Update.hasError()) {
        request->send(500, "text/plain", "Update failed.");
    } else if (!scheduleRestart()) {
        request->send(500, "text/plain", "Update stored, but the restart could not be scheduled.");
    } else {
        request->send(200, "text/plain", "Update successful. Rebooting...");
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
    fbauth::Sha256* sha;     // of the file as it arrives (see announcedSha256)
    uint8_t want[32];
    esp_err_t error;
    bool restart;  // the boot script was stored: the engine restarts with it
};

static void destroyWriter(BytecodeUpload* upload) {
    if (upload && upload->sha) {
        upload->sha->~Sha256();
        free(upload->sha);
        upload->sha = nullptr;
    }
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
        upload->sha = nullptr;
        upload->error = ESP_OK;
        upload->restart = false;
        request->_tempObject = upload;
        request->onDisconnect([request]() {
            destroyWriter(static_cast<BytecodeUpload*>(request->_tempObject));
        });

        // "size=<bytes>": the image's own size. The region is then taken for
        // exactly that; without it (an older IDE bundle, curl) for the
        // request's length, which includes the multipart framing.
        size_t image_size = content_length;
        bool exactSize = false;
        if (const AsyncWebParameter* p = request->getParam("size")) {
            uint32_t announced = 0;
            if (!bcstore::parseImageSize(p->value().c_str(), static_cast<uint32_t>(content_length),
                                         BytecodeRegistry::instance().capacity(), &announced)) {
                ESP_LOGE(TAG, "Bytecode upload refused: size=%s is not a possible image size", p->value().c_str());
                upload->error = ESP_ERR_INVALID_ARG;
                return;
            }
            image_size = announced;
            exactSize = true;
        }

        if (!announcedSha256(request, upload->want)) {
            ESP_LOGE(TAG, "Bytecode upload refused: the URL needs ?sha256=<SHA-256 of the file>");
            upload->error = ESP_ERR_INVALID_ARG;
            return;
        }
        void* shaMem = malloc(sizeof(fbauth::Sha256));
        if (!shaMem) {
            upload->error = ESP_ERR_NO_MEM;
            return;
        }
        upload->sha = new (shaMem) fbauth::Sha256();
        upload->sha->init();

        void* mem = malloc(sizeof(BytecodeWriter));
        if (!mem) {
            ESP_LOGE(TAG, "No heap for the bytecode writer");
            upload->error = ESP_ERR_NO_MEM;
            return;
        }
        upload->writer = new (mem) BytecodeWriter();

        // "atoms=<id>": the compiler resolved the built-in names for the ROM
        // atoms with that identity. When they are this build's, the writer
        // needs no lookup context (8 KB of heap less during the upload).
        bool atomsResolved = false;
        if (const AsyncWebParameter* p = request->getParam("atoms")) {
            atomsResolved = strtoul(p->value().c_str(), nullptr, 16) == bytecode_stdlib_atoms_id();
        }
        // Initialize with the size for the upfront flash erase
        const esp_err_t err = upload->writer->beginWrite(filename.c_str(), image_size, atomsResolved, exactSize);
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

    // The last chunk is only written (which makes the script valid) when
    // the whole file has the hash the signed URL announced.
    upload->sha->update(data, len);
    if (final) {
        uint8_t got[32];
        upload->sha->finish(got);
        if (!fbauth::digestEqual(got, upload->want, 32)) {
            ESP_LOGE(TAG, "Bytecode upload refused: the file does not have the announced SHA-256");
            upload->error = ESP_ERR_INVALID_CRC;
            destroyWriter(upload);  // drops what was written
            return;
        }
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

// GET /api/bytecode/list: the registry's active entries, generated like
// /config (sendGeneratedJson).
static void writeBytecodeList(WindowOutputStream& out) {
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
    sendGeneratedJson(request, writeBytecodeList);
}

// POST /api/reboot: answer first, then restart (scheduleRestart).
static void handleReboot(AsyncWebServerRequest *request) {
    if (!scheduleRestart()) {
        request->send(500, "application/json", R"({"status":"error","message":"timer"})");
        return;
    }
    ESP_LOGI(TAG, "Reboot requested over HTTP");
    request->send(200, "application/json", R"({"status":"rebooting"})");
}

void webserver_start() {
    // The web IDE is served from LittleFS /www/ (same origin) and needs no
    // CORS headers. A cross-origin client (dev: the IDE served by Vite) gets
    // them from the CORS middleware (fbauth::installGate), which also answers
    // every cross-origin OPTIONS (the preflight) itself, before any handler.
    // An OPTIONS from the same origin or without an Origin is not a
    // preflight and gets what any request without a route gets: 404 below,
    // 405 from the S3 handler for a bucket path.

    // SigV4 authentication (Auth.h, docs/AUTH.md). The gate must be the FIRST
    // handler: it takes over a protected request whose signature fails at the
    // end of its headers, before any body byte reaches a handler. Signature
    // required: POST /api/reboot, /upload, /ota_update,
    // /resetWifi, /api/bytecode/upload; GET /config; the /ws/repl upgrade.
    // Anonymous: /api/version, /api/device, /api/bytecode/list,
    // /api/bytecode/next_address, static www, captive portal, CORS
    // preflights (cross-origin OPTIONS).
    fbauth::init();
    fbauth::installGate(server);
    // Bodies nobody consumes are refused (RequestGuard.h).
    guard::install(server);

    // No route and no static file: 404. (The captive portal is answered
    // first.)
    server.onNotFound([](AsyncWebServerRequest *request){
        // Captive portal: OS connectivity probes (/hotspot-detect.html,
        // /generate_204, /connecttest.txt, ...) resolve to us via the portal's
        // DNS. Redirect every unknown GET to the WiFi setup page.
        if (is_captive_portal_active() && request->method() == HTTP_GET) {
            request->redirect("http://192.168.4.1/");
            return;
        }
        request->send(404);
    });

    // The API routes are registered BEFORE the S3 handler: a signed request
    // (Authorization: AWS4-HMAC-SHA256, x-amz-* headers) looks like an S3
    // client to it, and it would answer e.g. GET /config as bucket "config".
    server.on("/config", HTTP_GET, handleConfig);

    // Firmware/build metadata: FIRMWARE_VERSION is a define, the commit comes
    // from the generated fb_build_info.h (tools/scripts/build_info_flags.py).
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
        // The text is handed over, not copied. A cross-origin dev build
        // gets its CORS headers from the auth middleware.
        sendOrBusy(request, stream::beginText(FreeButton::toJson(info), "application/json"));
    });

    // POST only (and signed): as a GET it was a one-click WiFi wipe for any page.
    // Not on scheduleRestart(): reset_wifi_credentials() writes NVS, stops
    // WiFi and waits itself, which is too much for the esp_timer task.
    server.on("/resetWifi", HTTP_POST, [](AsyncWebServerRequest *request){
        request->send(200, "text/plain", "Resetting WiFi settings and rebooting...");
        delay(1000);
        reset_wifi_credentials();
    });

    // OTA endpoints
    // (The former /upgrade page is gone: its form could not sign the upload.)
    server.on("/upload", HTTP_POST, handleFirmwareUpload, onFileUpload);
    server.on("/ota_update", HTTP_POST, handleOtaUpdate);
    // (The former /updatefirmware, a download from a firmware server, is gone.)

    // Bytecode upload endpoints (the former /bytecode dev page was removed:
    // its unsigned upload was refused; upload from the IDE).

    server.on("/api/bytecode/upload", HTTP_POST, handleBytecodeUpload, onBytecodeUpload);
    server.on("/api/bytecode/list", HTTP_GET, handleBytecodeList);
    server.on("/api/reboot", HTTP_POST, handleReboot);

    // Measurements: heapLow of /api/device counts from this call on.
    server.on("/api/heap/mark", HTTP_POST, [](AsyncWebServerRequest *request){
        FreeButton::DeviceInfoCollector::markHeapLow();
        request->send(200, "application/json", R"({"status":"marked"})");
    });
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

    // What the IDE's compiler needs to know of this build (the name is from
    // the time the IDE relocated bytecode itself; the device now places and
    // relocates uploads). rom_base / atoms_id: where this build's stdlib ROM
    // table is and which ROM atoms it has. An IDE whose compiler has the same
    // atoms resolves them for this address (BytecodeWriter.cpp).
    server.on("/api/bytecode/next_address", HTTP_GET, [](AsyncWebServerRequest *request){
        char body[64];
        int n = snprintf(body, sizeof(body),
            "{\"rom_base\":\"0x%lx\",\"atoms_id\":\"0x%lx\"}",
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

    // The web IDE (LittleFS /www/), last: every GET whose file exists there.
    // index.html is the document for "/"; "<file>.gz" is sent for "<file>".
    // Files with a content hash in their name and the IDE's web fonts may be
    // kept by the browser, the rest is checked again every time (ETag).
    freebutton::registerStaticFiles(server);

    ESP_LOGI(TAG, "Starting webserver");
    server.begin();
}