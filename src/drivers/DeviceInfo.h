#pragma once

#include <string>
#include <memory>

namespace FreeButton {

struct DeviceInfo {
    std::string deviceName;
    std::string ipv4Address;
    std::string ipv6Address;
    size_t freeMemory;
    std::string connectedSSID;
    int signalStrength;  // RSSI in dBm
    std::string firmwareVersion;
    std::string macAddress;
    uint32_t uptime;  // seconds
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

    // Format device info for display
    static std::string formatDeviceInfo(const DeviceInfo& info);
};


} // namespace FreeButton