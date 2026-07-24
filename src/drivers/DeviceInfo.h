#pragma once

#include <string>
#include <memory>

namespace FreeButton {

struct DeviceInfo {
    std::string deviceName;
    std::string ipv4Address;
    std::string ipv6Address;
    std::string connectedSSID;
    int signalStrength;  // RSSI in dBm
    std::string firmwareVersion;
    std::string macAddress;
    uint32_t uptime;     // seconds

    // Memory breakdown, all in BYTES. The web IDE divides by 1024 for display.
    // heapFree/heapTotal/heapMinFree are all measured over MALLOC_CAP_INTERNAL
    // so free/total share one region and their ratio is meaningful (never > 1).
    size_t heapFree;     // internal heap free now
    size_t heapTotal;    // internal heap total
    size_t heapMinFree;  // internal heap low-water mark since boot
    size_t lvglFree;     // LVGL allocator free bytes
    size_t lvglTotal;    // LVGL allocator total bytes
};

class DeviceInfoCollector {
public:
    DeviceInfoCollector() = default;
    ~DeviceInfoCollector() = default;

    // Collect all device information
    DeviceInfo collectInfo() const;

    // Individual info collection methods
    static std::string getDeviceName();

    static std::string getIPv4Address();

    static std::string getIPv6Address();

    static size_t getFreeMemory();

    static std::string getConnectedSSID();

    static int getSignalStrength();
    std::string getFirmwareVersion() const;

    static std::string getMacAddress();

    static uint32_t getUptime();

    static size_t getTotalHeap();

    static size_t getMinFreeHeap();

    // Fills lvglFree/lvglTotal from the LVGL allocator monitor (0 if unavailable).
    static void getLvglMemory(size_t& freeBytes, size_t& totalBytes);

    // Format device info for display
    static std::string formatDeviceInfo(const DeviceInfo& info);
};

// Serialize a DeviceInfo to a JSON string for the /api/device endpoint.
//
// Kept as a free function in its own translation unit (DeviceInfoJson.cpp) so it
// pulls in NO hardware headers (WiFi/esp/lvgl) and can be unit-tested natively.
// Memory fields are emitted in bytes; the web IDE applies the KB quantifier.
std::string toJson(const DeviceInfo& info);


} // namespace FreeButton