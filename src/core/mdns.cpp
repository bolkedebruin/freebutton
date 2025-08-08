//
// Created by Bolke de Bruin on 08/08/2025.
//
#include "mdns.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <config.h>

static const auto TAG = "mdns";

void mdns_start()
{
    // Start the mDNS responder for {device_name}-button.local
    if (!MDNS.begin(deviceId.c_str())) {
        ESP_LOGW(TAG, "Error setting up mDNS responder");
        return; // Exit if mDNS setup fails
    }
    ESP_LOGI(TAG, "mDNS responder started for %s.local", deviceId.c_str());

    // Set an instance name
    const String macAddress = WiFi.macAddress();
    String instanceName = "ButtonPlus_" + macAddress;
    instanceName.replace(":", "");
    MDNS.setInstanceName(instanceName);

    // Add HTTP service to MDNS-SD
    MDNS.addService("_http", "_tcp", 80);

    // Add TXT records for service discovery
    MDNS.addServiceTxt("_http", "_tcp", "id", macAddress);
    MDNS.addServiceTxt("_http", "_tcp", "md", "Button+");
    MDNS.addServiceTxt("_http", "_tcp", "path", "/"); // Root path for HTTP service

    // Add version and other metadata
    // MDNS.addServiceTxt("_http", "_tcp", "version", "1.0");
}

void mdns_stop()
{
    ESP_LOGI(TAG, "Stopping mDNS responder");
    MDNS.end();
}
