//
// Created by Bolke de Bruin on 06/08/2025.
//
#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <esp_log.h>

#include "mdns.h"
#include "../ui/wifi_qr_modal.h"

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

    // Show QR code modal for WiFi setup
    show_wifi_qr_modal(lv_screen_active(), AP_SSID, AP_PASSWORD);

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

                    // Hide QR modal on successful connection
                    hide_wifi_qr_modal();

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
            ESP_LOGW(TAG, "WiFi Disconnected!");
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
        WiFi.softAPenableIPv6();
        WiFi.enableIPv6();
        WiFi.begin(savedCreds.ssid, savedCreds.password);

        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20) {
            delay(500);
            ESP_LOGD(TAG, "Connecting attempt %d/20", attempts + 1);
            attempts++;
        }

        if (WiFi.status() == WL_CONNECTED) {
            ESP_LOGI(TAG, "WiFi Connected!");

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

void wifi_event_handler(WiFiEvent_t event, WiFiEventInfo_t info) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_READY:
            ESP_LOGI(TAG, "WiFi interface ready");
            break;
        case ARDUINO_EVENT_WIFI_SCAN_DONE:
            ESP_LOGI(TAG, "Completed scan for access points");
            break;
        case ARDUINO_EVENT_WIFI_STA_START:
            ESP_LOGI(TAG, "WiFi client started");
            break;
        case ARDUINO_EVENT_WIFI_STA_STOP:
            ESP_LOGI(TAG, "WiFi client stopped");
            break;
        case ARDUINO_EVENT_WIFI_STA_CONNECTED:
            ESP_LOGI(TAG, "Connected to access point");
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            ESP_LOGW(TAG, "Disconnected from WiFi access point, reason: %d", info.wifi_sta_disconnected.reason);
            wifiConnected = false;
            // Stop mDNS when disconnected
            mdns_stop();
            break;
        case ARDUINO_EVENT_WIFI_STA_AUTHMODE_CHANGE:
            ESP_LOGI(TAG, "Authentication mode of access point has changed");
            break;
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            ESP_LOGI(TAG, "Obtained IPv4 address: " IPSTR, IP2STR(&info.got_ip.ip_info.ip));
            if (!wifiConnected) {
                ESP_LOGI(TAG, "Starting mDNS responder");
                mdns_start();
            } else {
                ESP_LOGI(TAG, "WiFi already connected, skipping mDNS start");
            }
            wifiConnected = true;
            // Start mDNS when we get IP
            break;
        case ARDUINO_EVENT_WIFI_STA_LOST_IP:
            ESP_LOGW(TAG, "Lost IP address and IP address is reset to 0");
            wifiConnected = false;
            // Stop mDNS when we lose IP
            mdns_stop();
            break;
        case ARDUINO_EVENT_WIFI_STA_GOT_IP6:
            ESP_LOGI(TAG, "IPv6 Global Address: %s", WiFi.globalIPv6().toString().c_str());
            ESP_LOGI(TAG, "IPv6 Link Local Address: %s", WiFi.linkLocalIPv6().toString().c_str());
            if (!wifiConnected) {
                ESP_LOGI(TAG, "Starting mDNS responder");
                mdns_start();
            } else {
                ESP_LOGI(TAG, "WiFi already connected, skipping mDNS start");
            }
            wifiConnected = true;

            break;
        case ARDUINO_EVENT_WIFI_AP_START:
            ESP_LOGI(TAG, "WiFi access point started");
            break;
        case ARDUINO_EVENT_WIFI_AP_STOP:
            ESP_LOGI(TAG, "WiFi access point stopped");
            break;
        case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
            ESP_LOGI(TAG, "Client " MACSTR " connected", MAC2STR(info.wifi_ap_staconnected.mac));
            break;
        case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
            ESP_LOGI(TAG, "Client " MACSTR " disconnected", MAC2STR(info.wifi_ap_stadisconnected.mac));
            break;
        case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:
            ESP_LOGI(TAG, "Assigned IP address to client");
            break;
        case ARDUINO_EVENT_WIFI_AP_PROBEREQRECVED:
            ESP_LOGD(TAG, "Received probe request");
            break;
        case ARDUINO_EVENT_WIFI_AP_GOT_IP6:
            ESP_LOGI(TAG, "AP IPv6 is preferred");
            ESP_LOGI(TAG, "IPv6 Global Address: %s", WiFi.globalIPv6().toString().c_str());
            ESP_LOGI(TAG, "IPv6 Link Local Address: %s", WiFi.linkLocalIPv6().toString().c_str());
            break;
        default:
            ESP_LOGD(TAG, "Unhandled WiFi event: %d", event);
            break;
    }
}

void setup_wifi_event_handler() {
    WiFi.onEvent(wifi_event_handler);
    ESP_LOGI(TAG, "WiFi event handler registered");
}