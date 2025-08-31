//
// Created by Bolke de Bruin on 31/08/2025.
//

#pragma once

#include <unordered_map>
#include <functional>
#include <memory>
#include <optional>
#include <esp_log.h>
#include <Wire.h>

namespace ButtonPlus {

enum class DeviceType : uint8_t {
    UNKNOWN = 0,
    BUTTON_BAR_DUAL = 1,
    BUTTON_BAR_LARGE = 2, 
    STS35_TEMPERATURE = 3,
    BME280_SENSOR = 4
};

struct I2CDevice {
    uint8_t address;
    DeviceType type;
    std::string description;
    TwoWire* wire;  // Wire or Wire1
    
    I2CDevice(uint8_t addr, DeviceType dev_type, std::string desc, TwoWire* w)
        : address(addr), type(dev_type), description(std::move(desc)), wire(w) {}
};

enum class ConnectorType : uint8_t { 
    EMPTY = 0, 
    BAR = 1, 
    LARGE = 2 
};

class HardwareScanner {
public:
    using DeviceDetectedCallback = std::function<void(const I2CDevice&)>;
    using ConnectorDetectedCallback = std::function<void(int connector, ConnectorType type)>;

    HardwareScanner();
    ~HardwareScanner() = default;

    // Non-copyable, movable
    HardwareScanner(const HardwareScanner&) = delete;
    HardwareScanner& operator=(const HardwareScanner&) = delete;
    HardwareScanner(HardwareScanner&&) = default;
    HardwareScanner& operator=(HardwareScanner&&) = default;

    // Scan functions
    void scanI2CDevices();
    void scanConnectors();
    void scanAll();

    // Callback registration
    void onDeviceDetected(DeviceDetectedCallback callback);
    void onConnectorDetected(ConnectorDetectedCallback callback);

    // Getters
    [[nodiscard]] const auto& getI2CDevices() const { return i2cDevices_; }
    [[nodiscard]] const auto& getConnectors() const { return connectors_; }
    [[nodiscard]] std::optional<ConnectorType> getConnectorType(int connector) const;
    [[nodiscard]] bool hasButtonBars() const;

private:
    static constexpr const char* TAG = "HardwareScanner";
    static constexpr uint8_t BUTTON_BAR_BASE_ADDRESS = 0x20;
    static constexpr int MAX_CONNECTORS = 4;

    std::unordered_map<uint8_t, I2CDevice> i2cDevices_;
    std::unordered_map<int, ConnectorType> connectors_;
    
    DeviceDetectedCallback deviceCallback_;
    ConnectorDetectedCallback connectorCallback_;

    [[nodiscard]] static DeviceType identifyDevice(uint8_t address);
    [[nodiscard]] static std::string getDeviceDescription(DeviceType type);
    [[nodiscard]] static bool probeI2CAddress(uint8_t address, TwoWire& wire);
};

} // namespace ButtonPlus