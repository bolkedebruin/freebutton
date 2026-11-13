#include "DeviceInfo.h"
#include <WiFi.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <lvgl.h>
#include "LvglDisplay.h"
extern "C" {
#include "ui/ui_text.h"
}
#include "../scripting/device_hardware.h"
#include "../scripting/ScriptEngine.h"

#include "config.h"

namespace FreeButton {

DeviceInfo DeviceInfoCollector::collectInfo() const {
    DeviceInfo info;

    info.deviceName = getDeviceName();
    info.ipv4Address = getIPv4Address();
    info.ipv6Address = getIPv6Address();
    info.connectedSSID = getConnectedSSID();
    info.signalStrength = getSignalStrength();
    info.firmwareVersion = getFirmwareVersion();
    info.macAddress = getMacAddress();
#ifdef GIT_COMMIT
    info.gitCommit = GIT_COMMIT;
#endif
#ifdef BUILD_DIRTY
    info.dirty = BUILD_DIRTY != 0;
#endif
#ifdef MODEL
    info.model = MODEL;
#endif
    info.uptime = getUptime();

    info.heapFree = getFreeMemory();
    info.heapTotal = getTotalHeap();
    info.heapMinFree = getMinFreeHeap();
    getLvglMemory(info.lvglFree, info.lvglTotal);
    ui_text_stats(&info.textCollapsed, &info.textSkipped);  // two word reads, no lock needed
    const auto js = Scripting::ScriptEngine::getInstance().heapUsage();
    info.jsHeapUsed = js.used;
    info.jsHeapFree = js.free;
    info.jsHeapSize = js.size;
    for (int c = 0; c < DeviceInfo::kConnectors; ++c) {
        info.connectorTypes[c] = static_cast<int8_t>(device_hw_get_connector_type(c));
    }
    info.mainConnector = static_cast<int8_t>(device_hw_get_main_connector());
    return info;
}

std::string DeviceInfoCollector::getDeviceName() {
    // Try to get device name from WiFi hostname, fallback to default
    return deviceId;
}

std::string DeviceInfoCollector::getIPv4Address() {
    if (WiFi.status() == WL_CONNECTED) {
        return WiFi.localIP().toString().c_str();
    }
    return "Not connected";
}

std::string DeviceInfoCollector::getIPv6Address() {
    // IPv6 support varies by ESP32 version, fallback to "Not available"
    return WiFi.globalIPv6().toString().c_str();
}

size_t DeviceInfoCollector::getFreeMemory() {
    // Internal SRAM free, matching getTotalHeap()/getMinFreeHeap()'s cap so the
    // free/total ratio the web graph plots stays within [0, 1].
    return heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
}

std::string DeviceInfoCollector::getConnectedSSID() {
    if (WiFi.status() == WL_CONNECTED) {
        return WiFi.SSID().c_str();
    }
    return "Not connected";
}

int DeviceInfoCollector::getSignalStrength() {
    if (WiFi.status() == WL_CONNECTED) {
        return WiFi.RSSI();
    }
    return 0;
}

std::string DeviceInfoCollector::getFirmwareVersion() const {
    // FIRMWARE_VERSION comes from the repo-root VERSION file via
    // tools/scripts/build_info_flags.py. (This used to test an undefined
    // VERSION macro, so /api/device always said "1.0.0".)
    #ifdef FIRMWARE_VERSION
        return FIRMWARE_VERSION;
    #else
        return "unknown";
    #endif
}

std::string DeviceInfoCollector::getMacAddress() {
    return WiFi.macAddress().c_str();
}

uint32_t DeviceInfoCollector::getUptime() {
    return millis() / 1000;  // Convert to seconds
}

size_t DeviceInfoCollector::getTotalHeap() {
    return heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
}

size_t DeviceInfoCollector::getMinFreeHeap() {
    return heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
}

void DeviceInfoCollector::getLvglMemory(size_t& freeBytes, size_t& totalBytes) {
    // Snapshot from the LVGL task: lv_mem_monitor() itself walks the pool and
    // must not run outside the LVGL lock (this is called from async_tcp).
    getLvglDisplay().getLvglMemory(freeBytes, totalBytes);
}

std::string DeviceInfoCollector::formatDeviceInfo(const DeviceInfo& info) {
    char buffer[500];  // Reduced buffer size

    // Format memory in KB
    const size_t memoryKB = info.heapFree / 1024;

    // Format uptime
    const uint32_t hours = info.uptime / 3600;
    const uint32_t minutes = (info.uptime % 3600) / 60;
    const uint32_t seconds = info.uptime % 60;

    snprintf(buffer, sizeof(buffer),
        "Device: %s\n"
        "IPv4: %s\n"
        "IPv6: %s\n"
        "SSID: %s\n"
        "Signal Strength: %d dBm\n"
        "Free Memory: %zu KB\n"
        "MAC Address: %s\n"
        "Firmware: %s\n"
        "Uptime: %02luh:%02lum:%02lus\n",
        info.deviceName.c_str(),
        info.ipv4Address.c_str(),
        info.ipv6Address.c_str(),
        info.connectedSSID.c_str(),
        info.signalStrength,
        memoryKB,
        info.macAddress.c_str(),
        info.firmwareVersion.c_str(),
        hours, minutes, seconds
    );

    return std::string(buffer);
}

} // namespace FreeButton