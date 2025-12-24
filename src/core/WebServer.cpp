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
#include <esp_log.h>

static auto TAG = "webserver";

// External reference to global ConfigManager
extern freebutton::ConfigManager componentManager;

AsyncWebServer server(80);

void handleHomepage(AsyncWebServerRequest *request) {
    static constexpr std::string_view homepage_template = R"(
<!doctype html>
<html>
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width,initial-scale=1">
    <link href="https://fonts.googleapis.com/css?family=Fira+Sans:400,600,700,800&display=swap" rel="stylesheet">
    <title>Button +</title>
    <script defer="defer" src="https://button.plus/cdn/@cdn/runtime.js?v=@VERSION"></script>
    <script defer="defer" src="https://button.plus/cdn/@cdn/vendors.js?v=@VERSION"></script>
    <script defer="defer" src="https://button.plus/cdn/@cdn/main.js?v=@VERSION"></script>
    <script>window.esp32chipmodel="@model"</script>
</head>
<body>
    <div id="root">
        <div style="margin: 10px;">
            <h3>Loading application...</h3>
        </div>
    </div>
</body>
</html>
    )";

    std::string content{homepage_template};
    const std::string model = ESP.getChipModel();
    const std::string cdn = CDN;
    const std::string version = std::to_string(random(0, 10000));

    // Replace placeholders
    size_t pos = 0;
    while ((pos = content.find("@model", pos)) != std::string::npos) {
        content.replace(pos, 6, model);
        pos += model.length();
    }

    pos = 0;
    while ((pos = content.find("@cdn", pos)) != std::string::npos) {
        content.replace(pos, 4, cdn);
        pos += cdn.length();
    }

    pos = 0;
    while ((pos = content.find("@VERSION", pos)) != std::string::npos) {
        content.replace(pos, 8, version);
        pos += version.length();
    }

    request->send(200, "text/html", content.c_str());
}

void handleConfig(AsyncWebServerRequest *request) {
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    AsyncJsonOutputStream stream(response);

    // Write the configuration using ConfigManager's write method
    componentManager.write(stream);

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
    std::string content{upgradePage};
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

void webserver_start(void) {
    server.on("/", HTTP_GET, handleHomepage);
    server.on("/config", HTTP_GET, handleConfig);
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

    ESP_LOGI(TAG, "Starting webserver");
    server.begin();
}