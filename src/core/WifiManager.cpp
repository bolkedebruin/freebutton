//
// Created by Bolke de Bruin on 06/08/2025.
//
#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

static auto TAG = "wifi";

extern AsyncWebServer server;

DNSServer* dnsServer = nullptr;
Preferences wifiPrefs;

bool portalstarted = false;
bool wifiConnected = false;

TaskHandle_t wifiTaskHandle = nullptr;

static const char* AP_SSID = "Button+ AP";
static const char* AP_PASSWORD = "";

static const IPAddress apIP(192, 168, 4, 1);
static const IPAddress netMask(255, 255, 255, 0);

// WiFi credentials storage
struct WiFiCredentials {
    char ssid[33];
    char password[65];
};

WiFiCredentials savedCreds = {"", ""};

// HTML for WiFi configuration portal
static const char* wifiConfigHTML = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>Button+ WiFi Setup</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background-color: #f0f0f0; }
        .container { max-width: 400px; margin: 0 auto; background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
        h1 { text-align: center; color: #333; }
        .form-group { margin-bottom: 15px; }
        label { display: block; margin-bottom: 5px; color: #555; }
        input[type="text"], input[type="password"], select { width: 100%; padding: 8px; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box; }
        button { width: 100%; padding: 10px; background-color: #007bff; color: white; border: none; border-radius: 4px; cursor: pointer; margin-top: 10px; }
        button:hover { background-color: #0056b3; }
        .network-list { margin-bottom: 20px; }
        .network-item { padding: 8px; margin: 5px 0; background: #f8f9fa; border-radius: 4px; cursor: pointer; }
        .network-item:hover { background: #e9ecef; }
        .signal-strength { float: right; color: #666; }
        .status { text-align: center; margin-top: 15px; padding: 10px; border-radius: 4px; }
        .status.error { background-color: #f8d7da; color: #721c24; }
        .status.success { background-color: #d4edda; color: #155724; }
    </style>
</head>
<body>
    <div class="container">
        <h1>Button+ WiFi Setup</h1>
        <div class="network-list" id="networkList"></div>
        <form id="wifiForm">
            <div class="form-group">
                <label for="ssid">Network Name (SSID):</label>
                <input type="text" id="ssid" name="ssid" required>
            </div>
            <div class="form-group">
                <label for="password">Password:</label>
                <input type="password" id="password" name="password">
            </div>
            <button type="submit">Connect</button>
        </form>
        <div id="status" class="status" style="display: none;"></div>
    </div>
    <script>
        function scanNetworks() {
            fetch('/scan')
                .then(response => response.json())
                .then(data => {
                    const networkList = document.getElementById('networkList');
                    networkList.innerHTML = '';
                    data.networks.forEach(network => {
                        const div = document.createElement('div');
                        div.className = 'network-item';
                        div.innerHTML = `${network.ssid} <span class="signal-strength">${network.rssi}dBm</span>`;
                        div.onclick = () => document.getElementById('ssid').value = network.ssid;
                        networkList.appendChild(div);
                    });
                })
                .catch(error => console.error('Error scanning networks:', error));
        }

        document.getElementById('wifiForm').addEventListener('submit', function(e) {
            e.preventDefault();
            const ssid = document.getElementById('ssid').value;
            const password = document.getElementById('password').value;
            const status = document.getElementById('status');

            status.style.display = 'block';
            status.className = 'status';
            status.textContent = 'Connecting to WiFi...';

            fetch('/connect', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ssid: ssid, password: password})
            })
            .then(response => response.json())
            .then(data => {
                if (data.success) {
                    status.className = 'status success';
                    status.textContent = 'Connected successfully! Device will restart.';
                    setTimeout(() => window.location.reload(), 3000);
                } else {
                    status.className = 'status error';
                    status.textContent = data.message || 'Connection failed';
                }
            })
            .catch(error => {
                status.className = 'status error';
                status.textContent = 'Connection failed';
            });
        });

        scanNetworks();
        setInterval(scanNetworks, 10000);
    </script>
</body>
</html>
)rawliteral";

void loadWiFiCredentials() {
    wifiPrefs.begin("wifi", false);
    String ssid = wifiPrefs.getString("ssid", "");
    String password = wifiPrefs.getString("password", "");

    strncpy(savedCreds.ssid, ssid.c_str(), sizeof(savedCreds.ssid) - 1);
    strncpy(savedCreds.password, password.c_str(), sizeof(savedCreds.password) - 1);
    savedCreds.ssid[sizeof(savedCreds.ssid) - 1] = '\0';
    savedCreds.password[sizeof(savedCreds.password) - 1] = '\0';

    wifiPrefs.end();
}

void saveWiFiCredentials(const char* ssid, const char* password) {
    wifiPrefs.begin("wifi", false);
    wifiPrefs.putString("ssid", ssid);
    wifiPrefs.putString("password", password);
    wifiPrefs.end();

    strncpy(savedCreds.ssid, ssid, sizeof(savedCreds.ssid) - 1);
    strncpy(savedCreds.password, password, sizeof(savedCreds.password) - 1);
    savedCreds.ssid[sizeof(savedCreds.ssid) - 1] = '\0';
    savedCreds.password[sizeof(savedCreds.password) - 1] = '\0';
}

void start_wifimanager_portal() {
    ESP_LOGI(TAG, "Starting WiFi Configuration Portal");

    // do this via callbacks
    //set_modal("WIFI not connected\nAccess Point started\nUse Button+ AP to configure....");

    // Set up access point
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAPConfig(apIP, apIP, netMask);
    WiFi.softAP(AP_SSID, AP_PASSWORD);

    // Start DNS server for captive portal
    if (!dnsServer) {
        dnsServer = new DNSServer();
        dnsServer->start(53, "*", apIP);
    }

    // Add portal routes to existing webserver
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/html", wifiConfigHTML);
    });

    server.on("/scan", HTTP_GET, [](AsyncWebServerRequest *request) {
        int n = WiFi.scanNetworks();
        String json = "{\"networks\":[";
        for (int i = 0; i < n; ++i) {
            if (i > 0) json += ",";
            json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
        }
        json += "]}";
        request->send(200, "application/json", json);
    });

    server.on("/connect", HTTP_POST, [](AsyncWebServerRequest *request) {}, nullptr,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            String body = String((char*)data).substring(0, len);

            // Parse JSON manually for simplicity
            int ssidStart = body.indexOf("\"ssid\":\"") + 8;
            int ssidEnd = body.indexOf("\"", ssidStart);
            int passStart = body.indexOf("\"password\":\"") + 12;
            int passEnd = body.indexOf("\"", passStart);

            if (ssidStart > 7 && ssidEnd > ssidStart && passStart > 11 && passEnd > passStart) {
                String ssid = body.substring(ssidStart, ssidEnd);
                String password = body.substring(passStart, passEnd);

                // Save credentials
                saveWiFiCredentials(ssid.c_str(), password.c_str());

                // Try to connect
                WiFi.begin(ssid.c_str(), password.c_str());

                int attempts = 0;
                while (WiFi.status() != WL_CONNECTED && attempts < 20) {
                    delay(500);
                    attempts++;
                }

                if (WiFi.status() == WL_CONNECTED) {
                    // Enable IPv6
                    WiFi.enableIPv6();
                    
                    request->send(200, "application/json", "{\"success\":true,\"message\":\"Connected successfully\"}");
                    wifiConnected = true;

                    // Clean up portal
                    if (dnsServer) {
                        dnsServer->stop();
                        delete dnsServer;
                        dnsServer = nullptr;
                    }

                    // Switch to station mode
                    WiFi.mode(WIFI_STA);

                    // Restart to apply changes
                    delay(2000);
                    ESP.restart();
                } else {
                    request->send(200, "application/json", "{\"success\":false,\"message\":\"Connection failed\"}");
                }
            } else {
                request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid request\"}");
            }
        }
    );

    portalstarted = true;
}

void wifi_manager_monitor(void* parameter) {
    while (true) {
        if (dnsServer) {
            dnsServer->processNextRequest();
        }

        // Check WiFi connection status
        if (WiFi.status() == WL_CONNECTED && !wifiConnected) {
            // Enable IPv6 if not already enabled
            WiFi.enableIPv6();
            
            wifiConnected = true;
            ESP_LOGI(TAG, "WiFi Connected!");
            ESP_LOGI(TAG, "IPv4 Address: %s", WiFi.localIP().toString().c_str());
            ESP_LOGI(TAG, "IPv6 Address: %s", WiFi.globalIPv6().toString().c_str());

            // Clean up portal / dns if running
            if (dnsServer) {
                dnsServer->stop();
                delete dnsServer;
                dnsServer = nullptr;
            }

            WiFi.mode(WIFI_STA);
        } else if (WiFi.status() != WL_CONNECTED && wifiConnected) {
            wifiConnected = false;
            Serial.println("WiFi Disconnected!");
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void start_wifi_manager() {
    ESP_LOGI(TAG, "Setting up WiFi\n");

    // Load saved credentials
    loadWiFiCredentials();

    // Try to connect with saved credentials first
    if (strlen(savedCreds.ssid) > 0) {
        ESP_LOGI(TAG, "Attempting to connect with saved credentials...");
        WiFi.mode(WIFI_STA);
        WiFi.enableIPv6();
        WiFi.begin(savedCreds.ssid, savedCreds.password);

        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20) {
            delay(500);
            Serial.print(".");
            attempts++;
        }

        if (WiFi.status() == WL_CONNECTED) {
            ESP_LOGI(TAG, "WiFi Connected!");
            ESP_LOGI(TAG, "IPv4 Address: %s", WiFi.localIP().toString().c_str());
            ESP_LOGI(TAG, "IPv6 Address: %s", WiFi.globalIPv6().toString().c_str());
            
            wifiConnected = true;
            portalstarted = true;

            // Start background task for WiFi monitoring
            xTaskCreate(wifi_manager_monitor, "WiFiTask", 4096, nullptr, 1, &wifiTaskHandle);
            return;
        }
    }

    // If no saved credentials or connection failed, start portal
    ESP_LOGI(TAG, "No saved credentials or connection failed, starting configuration portal...");
    start_wifimanager_portal();

    // Start background task for portal and WiFi monitoring
    xTaskCreate(wifi_manager_monitor, "WiFiTask", 4096, nullptr, 1, &wifiTaskHandle);
}

void reset_wifi_credentials() {
    ESP_LOGW(TAG, "Resetting WiFi credentials\n");

    // Clear saved credentials
    wifiPrefs.begin("wifi", false);
    wifiPrefs.clear();
    wifiPrefs.end();

    // Clear in-memory credentials
    memset(&savedCreds, 0, sizeof(savedCreds));

    // Disconnect WiFi
    WiFi.disconnect(true, true);
    delay(500);

    ESP.restart();
}