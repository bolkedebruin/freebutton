//
// Created by Bolke de Bruin on 06/08/2025.
//
#include "config.h"
#include "ESPAsyncWebServer.h"
#include "WifiManager.h"
#include "components/ConfigManager.h"
#include "json/StreamWrappers.h"

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

void webserver_start(void) {
    server.on("/", HTTP_GET, handleHomepage);
    server.on("/config", HTTP_GET, handleConfig);
    server.on("/resetWifi", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "text/plain", "Resetting WiFi settings and rebooting...");
        delay(1000);
        reset_wifi_credentials();
    });
    ESP_LOGI(TAG, "Starting webserver");
    server.begin();
}