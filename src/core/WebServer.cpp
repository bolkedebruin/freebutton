//
// Created by Bolke de Bruin on 06/08/2025.
//
#include "config.h"
#include "ESPAsyncWebServer.h"
#include "WifiManager.h"
#include "components/ConfigManager.h"
#include "json/StreamWrappers.h"
#include "Update.h"
#include "OtaManager.h"
#include "scripting/BytecodeWriter.h"
#include "scripting/BytecodeRegistry.h"
#include "scripting/WebSocketREPL.h"
#include "drivers/DeviceInfo.h"
#include <ArduinoJson.h>
#include <esp_log.h>
#include <LittleFS.h>

static auto TAG = "webserver";

// External reference to global ConfigManager
extern freebutton::ConfigManager configManager;

AsyncWebServer server(80);

void handleConfig(AsyncWebServerRequest *request) {
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    AsyncJsonOutputStream stream(response);

    // Write the configuration using ConfigManager's write method
    configManager.write(stream);

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

// Function to handle file uploads
void onFileUpload(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
    static std::string updateType;
    if (!index) {
        ESP_LOGI(TAG, "Upload start for: %s", filename.c_str());

        // Get update type from request
        updateType = std::string(request->arg("updateType").c_str());
        if (updateType == "firmware") {
            ESP_LOGI(TAG, "Starting firmware update");
            if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                Update.printError(Serial);
                ESP_LOGE(TAG, "Update.begin() failed");
            }
        } else if (updateType == "filesystem") {
            ESP_LOGI(TAG, "Starting filesystem update");
            if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_SPIFFS)) {
                Update.printError(Serial);
                ESP_LOGE(TAG, "Update.begin() failed");
            }
        }
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
    if (Update.hasError()) {
        request->send(500, "text/plain", "Upload failed.");
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
// Use a small heap-allocated structure to store upload status
struct UploadStatus {
    esp_err_t error;
};

void onBytecodeUpload(AsyncWebServerRequest *request, const String& filename,
                      size_t index, uint8_t *data, size_t len, bool final) {
    BytecodeWriter* writer = nullptr;
    esp_err_t err = ESP_OK;

    if (index == 0) {
        size_t content_length = request->contentLength();
        ESP_LOGI(TAG, "Bytecode upload start: %s (%zu bytes)", filename.c_str(), content_length);

        // Create new writer and store in request
        writer = new BytecodeWriter();

        // Initialize with total size for upfront flash erase
        err = writer->beginWrite(filename.c_str(), content_length);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "beginWrite failed: 0x%x", err);
            delete writer;
            request->_tempObject = new UploadStatus{err};
            return;
        }

        request->_tempObject = writer;
    } else {
        // Retrieve existing writer from request
        writer = static_cast<BytecodeWriter*>(request->_tempObject);
    }

    if (!writer) {
        ESP_LOGE(TAG, "BytecodeWriter is null");
        return;
    }

    // Write chunk directly to flash with inline relocation
    err = writer->write(data, len, final);

    if (final) {
        ESP_LOGI(TAG, "Bytecode upload complete: %s", filename.c_str());

        // Allocate status structure for response handler
        UploadStatus* status = new UploadStatus{err};

        // Clean up writer and replace with status
        delete writer;
        request->_tempObject = status;
    } else if (err != ESP_OK) {
        // Upload failed mid-stream, clean up
        ESP_LOGE(TAG, "Upload failed during chunk write: 0x%x", err);

        // Allocate status structure for response handler
        UploadStatus* status = new UploadStatus{err};

        delete writer;
        request->_tempObject = status;
    }
}

void handleBytecodeUpload(AsyncWebServerRequest *request) {
    const auto status = static_cast<UploadStatus*>(request->_tempObject);

    if (!status) {
        request->send(500, "application/json", R"({"status":"error","message":"Upload failed"})");
        return;
    }

    esp_err_t err = status->error;
    delete status;
    request->_tempObject = nullptr;

    if (err == ESP_OK) {
        request->send(200, "application/json", R"({"status":"success"})");
    } else {
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        response->printf(R"({"status":"error","message":"Write failed","code":%d})", err);
        request->send(response);
    }
}

// Embedded WebSocket test page - served from device to avoid PNA restrictions
static constexpr std::string_view wsTestPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>WebSocket REPL Test</title>
    <meta charset="utf-8">
    <style>
        body { font-family: sans-serif; margin: 20px; }
        #log { background: #f0f0f0; padding: 10px; height: 300px; overflow: auto; font-family: monospace; }
        input { width: 300px; padding: 5px; }
        button { padding: 5px 15px; margin: 5px 0; }
    </style>
</head>
<body>
    <h1>WebSocket REPL Test</h1>
    <p><strong>Page origin:</strong> <span id="origin"></span></p>
    <input type="text" id="code" placeholder="Enter JavaScript code..." value="1+1">
    <button onclick="sendCode()">Evaluate</button>
    <button onclick="connect()">Reconnect</button>
    <button onclick="disconnect()">Disconnect</button>
    <pre id="log"></pre>
    <script>
        let ws = null;
        const log = (msg) => {
            const el = document.getElementById('log');
            el.textContent += new Date().toLocaleTimeString() + ': ' + msg + '\n';
            el.scrollTop = el.scrollHeight;
        };
        const connect = () => {
            if (ws) { ws.close(); ws = null; }
            const wsUrl = 'ws://' + location.host + '/ws/repl';
            log('Connecting to ' + wsUrl);
            ws = new WebSocket(wsUrl);
            ws.onopen = () => log('CONNECTED');
            ws.onclose = (e) => log('CLOSED: code=' + e.code);
            ws.onerror = () => log('ERROR');
            ws.onmessage = (e) => {
                const msg = JSON.parse(e.data);
                if (msg.type === 'result') {
                    log('=> ' + (msg.success ? msg.value : 'Error: ' + msg.error));
                } else if (msg.type === 'console') {
                    log('[console] ' + msg.message);
                } else {
                    log('MSG: ' + e.data);
                }
            };
        };
        const disconnect = () => { if (ws) { ws.close(); ws = null; } };
        const sendCode = () => {
            if (!ws || ws.readyState !== WebSocket.OPEN) { log('Not connected'); return; }
            const code = document.getElementById('code').value;
            const id = Math.random().toString(36).substr(2, 8);
            log('> ' + code);
            ws.send(JSON.stringify({ type: 'eval', id: id, code: code }));
        };
        document.getElementById('code').addEventListener('keypress', (e) => {
            if (e.key === 'Enter') sendCode();
        });
        document.getElementById('origin').textContent = location.origin;
        connect();
    </script>
</body>
</html>
)rawliteral";

void handleWsTestPage(AsyncWebServerRequest *request) {
    const std::string content{wsTestPage};
    request->send(200, "text/html", content.c_str());
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
    // have been removed.
    // The HTTP_OPTIONS -> 200 preflight guard is kept in onNotFound for any
    // remaining tooling that issues a CORS preflight against the API.

    // SPA fallback: any GET that is not an API/WebSocket route and does not map
    // to a static file serves the gzipped app shell so client-side routing works.
    server.onNotFound([](AsyncWebServerRequest *request){
        if (request->method() == HTTP_OPTIONS) {
            request->send(200);
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

    // Serve the locally-built web IDE bundle (with .gz auto-negotiation) from
    // LittleFS /www/. index.html is the default document for "/".
    server.serveStatic("/", LittleFS, "/www/").setDefaultFile("index.html");

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

    server.on("/resetWifi", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "text/plain", "Resetting WiFi settings and rebooting...");
        delay(1000);
        reset_wifi_credentials();
    });

    // OTA endpoints
    server.on("/upgrade", HTTP_GET, handleUpgradePage);
    server.on("/upload", HTTP_POST, handleFirmwareUpload, onFileUpload);
    server.on("/ota_update", HTTP_POST, handleOtaUpdate);
    server.on("/updatefirmware", HTTP_POST, handleRemoteFirmwareUpdate);

    // Bytecode upload endpoints
    server.on("/bytecode", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(LittleFS, "/bytecode_upload.html", "text/html");
    });

    server.on("/api/bytecode/upload", HTTP_POST, handleBytecodeUpload, onBytecodeUpload);

    // Reports where the next uploaded bytecode blob will land in the js_user
    // partition. The registry mmaps the partition in its constructor, so use a
    // function-local static (mapped once) and reload metadata on each request.
    server.on("/api/bytecode/next_address", HTTP_GET, [](AsyncWebServerRequest *request){
        static BytecodeRegistry registry;
        registry.load();
        const uint32_t offset = registry.getNextOffset();
        const uint32_t virtual_addr = registry.getVirtualBaseAddress() + offset;
        const size_t free_space = registry.getFreeSpace();
        char body[160];
        int n = snprintf(body, sizeof(body),
            "{\"offset\":\"0x%lx\",\"virtual_addr\":\"0x%lx\",\"free_space\":%zu}",
            (unsigned long)offset, (unsigned long)virtual_addr, free_space);
        if (n < 0 || n >= (int)sizeof(body)) {
            request->send(500, "application/json", "{\"error\":\"next_address overflow\"}");
            return;
        }
        request->send(200, "application/json", body);
    });

    // WebSocket test page (served from device to avoid browser PNA restrictions)
    server.on("/ws-test", HTTP_GET, handleWsTestPage);

    // WebSocket REPL for JavaScript debugging
    Scripting::WebSocketREPL::getInstance().init(server);

    ESP_LOGI(TAG, "Starting webserver");
    server.begin();
}