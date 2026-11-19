//
// Created by Bolke de Bruin on 06/08/2025.
//
#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_random.h>

#include "json/JsonEscape.h"
#include "mdns.h"
#include "../ui/wifi_qr_modal.h"
#include "../utils/SetupPassword.h"
#include "../drivers/LvglDisplay.h"

// Declared in WifiManager.h (not included here: its wifi_event_handler
// prototype clashes with this file's Arduino-style handler of the same name).
bool is_captive_portal_active();

static auto TAG = "wifi";

extern AsyncWebServer server;

DNSServer* dnsServer = nullptr;
Preferences wifiPrefs;

bool portalstarted = false;
bool wifiConnected = false;

TaskHandle_t wifiTaskHandle = nullptr;

static auto AP_SSID = "FreeButton";

// Password of the setup access point: random per device (form and alphabet in
// utils/SetupPassword.h), kept in NVS namespace "wifi", key "ap_pass", next to
// the WiFi credentials. It stays the same across reboots and firmware updates
// and is made again when it is missing: after reset_wifi_credentials() (which
// clears the namespace) or an NVS erase. It is shown on the display only
// (ui/wifi_qr_modal): never log it and never put it in an HTTP response.
static constexpr char kNvsApPassword[] = "ap_pass";
static char AP_PASSWORD[freebutton::kSetupPasswordLen + 1] = "";

static const IPAddress apIP(192, 168, 4, 1);
static const IPAddress netMask(255, 255, 255, 0);

// WiFi credentials storage
struct WiFiCredentials {
    char ssid[33];
    char password[65];
};

WiFiCredentials savedCreds = {"", ""};

// HTML for WiFi configuration portal
static auto wifiConfigHTML = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>FreeButton WiFi Setup</title>
    <meta name="viewport" content="width=device-width, initial-scale=1, user-scalable=no">
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background-color: #f0f0f0; }
        .container { max-width: 400px; margin: 0 auto; background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
        h1 { text-align: center; color: #333; }
        .form-group { margin-bottom: 15px; }
        label { display: block; margin-bottom: 5px; color: #555; }
        input[type="text"], input[type="password"], select { width: 100%; padding: 8px; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box; font-size: 16px; }
        button { width: 100%; padding: 10px; background-color: #007bff; color: white; border: none; border-radius: 4px; cursor: pointer; margin-top: 10px; }
        button:hover { background-color: #0056b3; }
        .network-list { margin-bottom: 20px; max-height: 200px; overflow-y: auto; border: 1px solid #ddd; border-radius: 4px; }
        .network-item { padding: 8px; margin: 5px 0; background: #f8f9fa; border-radius: 4px; cursor: pointer; display: flex; justify-content: space-between; align-items: center; }
        .network-item:hover { background: #e9ecef; }
        .network-ssid { flex-grow: 1; }
        .signal-strength { display: flex; align-items: center; gap: 2px; }
        .signal-bar { width: 3px; background: #ddd; border-radius: 1px; }
        .signal-bar.active { background: #007bff; }
        .signal-bar:nth-child(1) { height: 6px; }
        .signal-bar:nth-child(2) { height: 10px; }
        .signal-bar:nth-child(3) { height: 14px; }
        .signal-bar:nth-child(4) { height: 18px; }
        .signal-bar:nth-child(5) { height: 22px; }
        .scanning { text-align: center; color: #666; padding: 10px; border-bottom: 1px solid #ddd; background: #f8f9fa; }
        .scanning.empty { padding: 20px; border: none; }
        .spinner { border: 2px solid #f3f3f3; border-top: 2px solid #007bff; border-radius: 50%; width: 16px; height: 16px; animation: spin 1s linear infinite; display: inline-block; margin-right: 8px; vertical-align: middle; }
        @keyframes spin { 0% { transform: rotate(0deg); } 100% { transform: rotate(360deg); } }
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
        function getSignalBars(rssi) {
            let strength = 0;
            if (rssi >= -50) strength = 5;
            else if (rssi >= -60) strength = 4;
            else if (rssi >= -70) strength = 3;
            else if (rssi >= -80) strength = 2;
            else if (rssi >= -90) strength = 1;

            let bars = '';
            for (let i = 1; i <= 5; i++) {
                bars += `<div class="signal-bar ${i <= strength ? 'active' : ''}"></div>`;
            }
            return bars;
        }

        let isScanning = false;

        function addScanningIndicator() {
            const networkList = document.getElementById('networkList');
            let scanIndicator = networkList.querySelector('.scanning-indicator');

            if (!scanIndicator) {
                scanIndicator = document.createElement('div');
                scanIndicator.className = 'scanning scanning-indicator';
                scanIndicator.innerHTML = '<div class="spinner"></div>Scanning...';

                // Insert at the top if there are networks, or show as empty state
                if (networkList.children.length > 0) {
                    networkList.insertBefore(scanIndicator, networkList.firstChild);
                } else {
                    scanIndicator.className = 'scanning scanning-indicator empty';
                    scanIndicator.innerHTML = '<div class="spinner"></div>Scanning for networks...';
                    networkList.appendChild(scanIndicator);
                }
            }
        }

        function removeScanningIndicator() {
            const networkList = document.getElementById('networkList');
            const scanIndicator = networkList.querySelector('.scanning-indicator');
            if (scanIndicator) {
                scanIndicator.remove();
            }
        }

        function updateNetworkList(networks) {
            const networkList = document.getElementById('networkList');
            removeScanningIndicator();

            // Clear existing networks but preserve structure
            const existingNetworks = networkList.querySelectorAll('.network-item');
            existingNetworks.forEach(item => item.remove());

            // Sort by signal strength and take top 5
            networks.sort((a, b) => b.rssi - a.rssi);
            const topNetworks = networks.slice(0, 5);

            topNetworks.forEach(network => {
                const div = document.createElement('div');
                div.className = 'network-item';
                // The name comes from the air: as text, never as markup.
                const name = document.createElement('span');
                name.className = 'network-ssid';
                name.textContent = network.ssid;
                const bars = document.createElement('div');
                bars.className = 'signal-strength';
                bars.innerHTML = getSignalBars(network.rssi);
                div.append(name, bars);
                div.onclick = () => document.getElementById('ssid').value = network.ssid;
                networkList.appendChild(div);
            });
        }

        function scanNetworks() {
            if (isScanning) return;

            isScanning = true;
            addScanningIndicator();

            fetch('/scan')
                .then(response => response.json())
                .then(data => {
                    updateNetworkList(data.networks);
                })
                .catch(error => {
                    console.error('Error scanning networks:', error);
                    removeScanningIndicator();
                    const networkList = document.getElementById('networkList');
                    if (networkList.children.length === 0) {
                        networkList.innerHTML = '<div class="scanning empty">Failed to scan networks</div>';
                    }
                })
                .finally(() => {
                    isScanning = false;
                });
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

static bool qrModalPending = false;

static uint8_t randomByte() {
    uint8_t b;
    esp_fill_random(&b, 1);
    return b;
}

// Reads the setup password from NVS into AP_PASSWORD, or makes and stores a
// new one. Call it after WiFi has started: esp_fill_random() gives true
// random numbers only while the RF subsystem is on (ESP-IDF, "Random Number
// Generation"); before that it is a pseudo-random generator. rfStarted false
// (WiFi did not start): a password for this boot only, nothing stored, so the
// next boot makes one with the RF entropy source on.
static void loadOrCreateSetupPassword(const bool rfStarted) {
    wifiPrefs.begin("wifi", false);
    if (wifiPrefs.isKey(kNvsApPassword)) {
        char stored[sizeof(AP_PASSWORD)] = {};
        wifiPrefs.getString(kNvsApPassword, stored, sizeof(stored));
        if (freebutton::isValidSetupPassword(stored)) {
            memcpy(AP_PASSWORD, stored, sizeof(AP_PASSWORD));
            wifiPrefs.end();
            return;
        }
    }

    freebutton::generateSetupPassword(randomByte, AP_PASSWORD);
    if (!rfStarted) {
        ESP_LOGE(TAG, "WiFi did not start: setup password not stored (it changes at the next boot)");
    } else if (wifiPrefs.putString(kNvsApPassword, AP_PASSWORD) == 0) {
        ESP_LOGE(TAG, "could not store the setup password in NVS (it changes on every boot)");
    } else {
        ESP_LOGW(TAG, "new setup access point password generated; it is shown on the display");
    }
    wifiPrefs.end();
}

void start_wifimanager_portal() {
    ESP_LOGI(TAG, "Starting WiFi Configuration Portal");

    // Mark that QR modal should be shown (will be shown later after main screen loads)
    qrModalPending = true;

    // Set up access point. The mode change starts WiFi (RF on), which the
    // random generator needs: the password is read or made after it.
    const bool rfStarted = WiFi.mode(WIFI_AP_STA);
    if (AP_PASSWORD[0] == '\0') {
        loadOrCreateSetupPassword(rfStarted);
    }
    WiFi.softAPConfig(apIP, apIP, netMask);
    WiFi.softAP(AP_SSID, AP_PASSWORD);

    // Start DNS server for captive portal
    if (!dnsServer) {
        ESP_LOGI(TAG, "DNS Server started");
        dnsServer = new DNSServer();
        dnsServer->start(53, "", apIP);
    }

    // Add portal routes to existing webserver
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        // This route stays registered (and, being registered before the IDE's
        // serveStatic, wins "/") after WiFi connects without a reboot. Once the
        // portal has closed, hand "/" back to the web IDE.
        if (!is_captive_portal_active()) {
            request->redirect("/index.html");
            return;
        }
        request->send(200, "text/html", wifiConfigHTML);
    });

    // /scan and /connect belong to the setup portal only. The routes stay
    // registered after it has closed (no reboot follows), so they must
    // refuse then: /connect replaces the stored WiFi credentials.
    server.on("/scan", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!is_captive_portal_active()) {
            request->send(404);
            return;
        }
        const int n = WiFi.scanNetworks();
        String json = "{\"networks\":[";
        for (int i = 0; i < n; ++i) {
            if (i > 0) json += ",";
            json += "{\"ssid\":\"";
            json += FreeButton::escapeJsonString(WiFi.SSID(i).c_str()).c_str();  // any bytes can be in a name
            json += "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
        }
        json += "]}";
        request->send(200, "application/json", json);
    });

    server.on("/connect", HTTP_POST, [](AsyncWebServerRequest *request) {
            if (!is_captive_portal_active()) request->send(404);
        }, nullptr,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            if (!is_captive_portal_active()) return;  // answered 404 by the request handler
            // data is not terminated: take exactly len bytes.
            String body;
            body.concat(reinterpret_cast<const char *>(data), len);

            // Parse JSON manually for simplicity
            const int ssidStart = body.indexOf("\"ssid\":\"") + 8;
            const int ssidEnd = body.indexOf("\"", ssidStart);
            const int passStart = body.indexOf("\"password\":\"") + 12;
            int passEnd = body.indexOf("\"", passStart);

            if (ssidStart > 7 && ssidEnd > ssidStart && passStart > 11 && passEnd > passStart) {
                const String ssid = body.substring(ssidStart, ssidEnd);
                const String password = body.substring(passStart, passEnd);

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

                    // Hide QR modal on successful connection. This runs in the
                    // web server task: LVGL must only be touched under its lock.
                    FreeButton::getLvglDisplay().lock();
                    hide_wifi_qr_modal();
                    FreeButton::getLvglDisplay().unlock();
                    qrModalPending = false;

                    // Clean up portal
                    if (dnsServer) {
                        dnsServer->stop();
                        delete dnsServer;
                        dnsServer = nullptr;
                    }

                    // Switch to station mode
                    WiFi.mode(WIFI_STA);

                    // Restart to apply changes
                    //delay(2000);
                    //ESP.restart();
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

        // Show QR modal if pending and main screen is ready
        if (qrModalPending && FreeButton::getLvglDisplay().getMainDisplay()) {
            ESP_LOGI(TAG, "Showing delayed WiFi QR modal");
            // This is the WiFi monitor task, not the LVGL task: building the
            // modal without the LVGL lock raced the LVGL refresh and crashed
            // in the flex layout (LoadProhibited) on every unprovisioned boot.
            auto& lvgl = FreeButton::getLvglDisplay();
            lvgl.lock();
            lv_obj_t* main_screen = lv_display_get_screen_active(lvgl.getMainDisplay());
            if (main_screen) {
                show_wifi_qr_modal(main_screen, AP_SSID, AP_PASSWORD);
                qrModalPending = false;
            }
            lvgl.unlock();
        }

        // Check WiFi connection status
        if (WiFi.status() == WL_CONNECTED && !wifiConnected) {
            // Enable IPv6 if not already enabled
            WiFi.enableIPv6();
            
            wifiConnected = true;
            qrModalPending = false; // Clear pending QR modal if WiFi connects
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
            qrModalPending = false; // No need for QR modal if already connected

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

    // Clear saved credentials. This also removes the setup access point
    // password ("ap_pass"): the portal makes a new one after the restart.
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

bool is_wifi_connected() {return wifiConnected;}
bool is_portal_started() {return portalstarted;}
bool is_captive_portal_active() { return dnsServer != nullptr; }
