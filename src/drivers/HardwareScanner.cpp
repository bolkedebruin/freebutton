//
// Created by Bolke de Bruin on 31/08/2025.
//

#include "HardwareScanner.h"
#include "Sensors.h"
#include "Adafruit_MCP23X08.h"
#include "pins.h"
#include <algorithm>

namespace FreeButton {

HardwareScanner::HardwareScanner() {
    connectors_.reserve(MAX_CONNECTORS);
}

void HardwareScanner::scanI2CDevices() {
    ESP_LOGI(TAG, "Scanning I2C devices...");
    
    i2cDevices_.clear();
    
    // Scan Wire (addresses 0-127)
    ESP_LOGD(TAG, "Scanning Wire bus...");
    for (uint8_t address = 1; address < 128; ++address) {
        if (probeI2CAddress(address, Wire)) {
            const DeviceType type = identifyDevice(address);
            const auto description = getDeviceDescription(type);
            
            i2cDevices_.emplace(address, I2CDevice{address, type, description, &Wire});
            
            ESP_LOGD(TAG, "I2C device found on Wire at 0x%02X: %s", address, description.c_str());
            
            // Add sensor if applicable
            if (type == DeviceType::STS35_TEMPERATURE || type == DeviceType::BME280_SENSOR) {
                addSensor(address);
            }
            
            // Notify callback
            if (deviceCallback_) {
                deviceCallback_(i2cDevices_.at(address));
            }
        }
    }

    // Scan Wire1 (virtual addresses 128-255, actual addresses 0-127)
    ESP_LOGD(TAG, "Scanning Wire1 bus...");
    for (uint8_t address = 1; address < 128; ++address) {
        if (probeI2CAddress(address, Wire1)) {
            const DeviceType type = identifyDevice(address);
            const auto description = getDeviceDescription(type);
            const uint8_t virtual_address = address + 128;
            
            i2cDevices_.emplace(virtual_address, I2CDevice{address, type, description, &Wire1});
            
            ESP_LOGD(TAG, "I2C device found on Wire1 at 0x%02X: %s", address, description.c_str());
            
            // Add sensor if applicable
            if (type == DeviceType::STS35_TEMPERATURE || type == DeviceType::BME280_SENSOR) {
                addSensor(address);
            }
            
            // Notify callback
            if (deviceCallback_) {
                deviceCallback_(i2cDevices_.at(virtual_address));
            }
        }
    }
    
    ESP_LOGD(TAG, "I2C scan complete. Found %zu devices", i2cDevices_.size());
    
    // Log a summary of all found addresses for debugging
    ESP_LOGI(TAG, "Device summary:");
    for (const auto& [key, device] : i2cDevices_) {
        ESP_LOGI(TAG, "  0x%02X: %s (%s)", device.address, device.description.c_str(),
                 device.wire == &Wire ? "Wire" : "Wire1");
    }
}

void HardwareScanner::scanConnectors() {
    ESP_LOGI(TAG, "Scanning connectors...");
    
    connectors_.clear();
    
    for (int connector = 0; connector < MAX_CONNECTORS; ++connector) {
        const uint8_t address = BUTTON_BAR_BASE_ADDRESS + connector;
        const uint8_t virtual_address = address + 128;  // Wire1 virtual address
        
        ESP_LOGD(TAG, "Scanning connector %d at address 0x%02X", connector, address);
        
        // Check if device exists on Wire1
        if (i2cDevices_.contains(virtual_address)) {
            Adafruit_MCP23X08 mcp;
            mcp.begin_I2C(address, &Wire1);
            
            // Read signature to determine connector type
            uint8_t signature = 0;
            for (int pin = 0; pin < 8; ++pin) {
                mcp.pinMode(pin, INPUT_PULLUP);
                const uint8_t bit = mcp.digitalRead(pin);
                signature |= (bit << pin);
            }
            
            ESP_LOGD(TAG, "Connector %d signature: 0x%02X", connector, signature);
            
            // Determine connector type based on signature
            const bool isLarge = (signature & (1 << 1)) == 0;
            const ConnectorType type = isLarge ? ConnectorType::LARGE : ConnectorType::BAR;
            
            connectors_[connector] = type;
            
            const char* typeStr = (type == ConnectorType::LARGE) ? "TFT320 Large display" : "Dual mini display";
            ESP_LOGI(TAG, "Connector %d: %s", connector, typeStr);
            
            // Configure pins based on type
            if (type == ConnectorType::LARGE) {
                mcp.pinMode(MCP320_CS, OUTPUT);
                mcp.digitalWrite(MCP320_CS, HIGH);
            } else {
                mcp.pinMode(MCP160_CS_LEFT, OUTPUT);
                mcp.digitalWrite(MCP160_CS_LEFT, HIGH);
                mcp.pinMode(MCP160_CS_RIGHT, OUTPUT);
                mcp.digitalWrite(MCP160_CS_RIGHT, HIGH);
            }
            
            // Configure button pins
            mcp.pinMode(MCP_SWITCH_LEFT, INPUT_PULLUP);
            mcp.pinMode(MCP_SWITCH_RIGHT, INPUT_PULLUP);
            
            // Notify callback
            if (connectorCallback_) {
                connectorCallback_(connector, type);
            }
        } else {
            ESP_LOGD(TAG, "Connector %d: No device found", connector);
            connectors_[connector] = ConnectorType::EMPTY;
        }
    }
    
    ESP_LOGI(TAG, "Connector scan complete. Found %zu active connectors", 
             std::ranges::count_if(connectors_, [](const auto& pair) { 
                 return pair.second != ConnectorType::EMPTY; 
             }));
}

void HardwareScanner::scanAll() {
    scanI2CDevices();
    scanConnectors();
}

void HardwareScanner::onDeviceDetected(DeviceDetectedCallback callback) {
    deviceCallback_ = std::move(callback);
}

void HardwareScanner::onConnectorDetected(ConnectorDetectedCallback callback) {
    connectorCallback_ = std::move(callback);
}

std::optional<ConnectorType> HardwareScanner::getConnectorType(const int connector) const {
    if (const auto it = connectors_.find(connector); it != connectors_.end()) {
        return it->second;
    }
    return std::nullopt;
}

bool HardwareScanner::hasButtonBars() const {
    return std::ranges::any_of(connectors_, [](const auto& pair) {
        return pair.second == ConnectorType::BAR;
    });
}

DeviceType HardwareScanner::identifyDevice(const uint8_t address) {
    if (address >= 0x20 && address < 0x28) {
        return DeviceType::BUTTON_BAR_DUAL;  // Could be dual or large, determined later
    }
    if (address == 0x4A || address == 0x4B) {
        return DeviceType::STS35_TEMPERATURE;
    }
    if (address == 0x76) {
        return DeviceType::BME280_SENSOR;
    }
    return DeviceType::UNKNOWN;
}

std::string HardwareScanner::getDeviceDescription(const DeviceType type) {
    switch (type) {
        case DeviceType::BUTTON_BAR_DUAL:
            return "Button bar: Dual or Large";
        case DeviceType::STS35_TEMPERATURE:
            return "STS35 Temperature sensor";
        case DeviceType::BME280_SENSOR:
            return "BME280 Pressure and Humidity sensor";
        case DeviceType::UNKNOWN:
        default:
            return "Unknown device";
    }
}

bool HardwareScanner::probeI2CAddress(const uint8_t address, TwoWire& wire) {
    wire.beginTransmission(address);
    return wire.endTransmission() == 0;
}

} // namespace FreeButton