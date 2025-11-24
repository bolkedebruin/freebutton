//
// Created by Bolke de Bruin on 31/08/2025.
//

#include "HardwareInitializer.h"
#include <Wire.h>

#include "pins.h"
#include "drivers/DeviceInfoManager.h"
#include "drivers/Sensors.h"

namespace FreeButton {

// Global instance
std::unique_ptr<HardwareInitializer> g_hardwareInitializer;

HardwareInitializer::HardwareInitializer()
    : initialized_(false)
    , tft320Bar_(-1) {
    
    mcps_.reserve(MAX_CONNECTORS);
    
    // Register callbacks for hardware detection
    scanner_.onDeviceDetected([this](const I2CDevice& device) {
        onDeviceDetected(device);
    });
    
    scanner_.onConnectorDetected([this](int connector, ConnectorType type) {
        onConnectorDetected(connector, type);
    });
}

bool HardwareInitializer::initializeHardware() {
    ESP_LOGI(TAG, "Starting hardware initialization...");
    
    if (initialized_) {
        ESP_LOGW(TAG, "Hardware already initialized");
        return true;
    }
    
    bool success = true;
    
    // Step 1: Initialize I2C
    if (!initializeI2C()) {
        ESP_LOGE(TAG, "Failed to initialize I2C");
        success = false;
    }
    
    // Step 2: Scan for hardware
    scanner_.scanAll();
    
    // Step 3: Initialize MCPs
    if (success && !initializeMCPs()) {
        ESP_LOGE(TAG, "Failed to initialize MCPs");
        success = false;
    }
    
    // Step 4: Initialize buttons
    int buttonCount = 0;
    if (success) {
        buttonCount = initializeButtons();
        if (buttonCount == 0) {
            ESP_LOGE(TAG, "Failed to initialize buttons");
            success = false;
        }
    }
    
    // Step 5: Initialize displays
    if (success && !initializeDisplays()) {
        ESP_LOGW(TAG, "Display initialization had issues (non-fatal)");
        // Display failure is not fatal for the overall system
    }

    // Step 6: Initialize sensors
    if (success && !initializeSensors()) {
        ESP_LOGW(TAG, "Sensor initialization had issues (non-fatal)");
        // Sensor failure is not fatal for the overall system
    }

    // Step 7: Initialize device info button
    if (success && !initializeDeviceInfoButton()) {
        ESP_LOGW(TAG, "Device info button initialization had issues (non-fatal)");
        // Device info button failure is not fatal for the overall system
    }

    // Step 8: Start button monitoring
    if (success && buttonCount > 0) {
        buttonManager_.startMonitoring();
        ESP_LOGI(TAG, "Button monitoring started");
    } else if (success) {
        ESP_LOGW(TAG, "No buttons found, monitoring not started");
    }
    
    initialized_ = success;
    
    if (success) {
        ESP_LOGI(TAG, "Hardware initialization completed successfully");
        ESP_LOGI(TAG, "Found %zu I2C devices", scanner_.getI2CDevices().size());
        ESP_LOGI(TAG, "TFT320 bar at connector: %d", tft320Bar_);
    } else {
        ESP_LOGE(TAG, "Hardware initialization failed");
    }
    
    // Notify callback
    if (initCallback_) {
        initCallback_(success);
    }
    
    return success;
}

void HardwareInitializer::onInitComplete(InitCompleteCallback callback) {
    initCallback_ = std::move(callback);
}

bool HardwareInitializer::initializeI2C() {
    ESP_LOGI(TAG, "Initializing I2C buses...");
    
    // Initialize Wire (CH1 bus - for sensors like STS35)
    if (!Wire.begin(CH1_SDA, CH1_SCL)) {
        ESP_LOGE(TAG, "Failed to initialize CH1 I2C bus (Wire) on pins %d,%d", CH1_SDA, CH1_SCL);
        return false;
    }
    
    // Initialize Wire1 (Button bus - for button bars/MCPs)
    if (!Wire1.begin(BUTTON_SDA, BUTTON_SCL)) {
        ESP_LOGE(TAG, "Failed to initialize Button I2C bus (Wire1) on pins %d,%d", BUTTON_SDA, BUTTON_SCL);
        return false;
    }
    
    ESP_LOGI(TAG, "I2C buses initialized successfully (Wire: %d,%d | Wire1: %d,%d)", 
             CH1_SDA, CH1_SCL, BUTTON_SDA, BUTTON_SCL);
    return true;
}

bool HardwareInitializer::initializeMCPs() {
    ESP_LOGI(TAG, "Initializing MCP23008 devices...");
    
    // Create MCP instances for each connector
    for (int connector = 0; connector < MAX_CONNECTORS; ++connector) {
        const auto connectorType = scanner_.getConnectorType(connector);
        
        if (connectorType && *connectorType != ConnectorType::EMPTY) {
            auto mcp = std::make_shared<Adafruit_MCP23008>();
            const uint8_t address = 0x20 + connector;
            
            if (mcp->begin(address, &Wire1)) {
                mcps_.push_back(mcp);
                ESP_LOGI(TAG, "MCP23008 initialized for connector %d at address 0x%02X", 
                         connector, address);
                
                // Configure display chip select pins based on connector type
                if (*connectorType == ConnectorType::LARGE) {
                    mcp->pinMode(MCP320_CS, OUTPUT);
                    mcp->digitalWrite(MCP320_CS, HIGH);
                } else if (*connectorType == ConnectorType::BAR) {
                    mcp->pinMode(MCP160_CS_LEFT, OUTPUT);
                    mcp->digitalWrite(MCP160_CS_LEFT, HIGH);
                    mcp->pinMode(MCP160_CS_RIGHT, OUTPUT);
                    mcp->digitalWrite(MCP160_CS_RIGHT, HIGH);
                }
                
                // Configure button pins
                mcp->pinMode(MCP_SWITCH_LEFT, INPUT);
                mcp->pullUp(MCP_SWITCH_LEFT, HIGH);
                mcp->pinMode(MCP_SWITCH_RIGHT, INPUT);
                mcp->pullUp(MCP_SWITCH_RIGHT, HIGH);
                
            } else {
                ESP_LOGE(TAG, "Failed to initialize MCP23008 for connector %d", connector);
                return false;
            }
        } else {
            // Add nullptr for empty connectors to maintain indexing
            mcps_.push_back(nullptr);
        }
    }
    
    ESP_LOGI(TAG, "MCP initialization completed");
    return true;
}

int HardwareInitializer::initializeButtons() {
    ESP_LOGI(TAG, "Initializing buttons...");
    
    int buttonCount = 0;
    
    // Create buttons for each active connector
    for (int connector = 0; connector < MAX_CONNECTORS; ++connector) {
        const auto connectorType = scanner_.getConnectorType(connector);
        
        if (connectorType && *connectorType != ConnectorType::EMPTY) {
            const auto mcp = getMCPForConnector(connector);
            
            if (mcp) {
                // Left button (even ID)
                const int leftId = connector * 2;
                auto leftButton = std::make_unique<PushButton>(leftId, MCP_SWITCH_LEFT, mcp);
                buttonManager_.addButton(std::move(leftButton));
                buttonCount++;
                
                // Right button (odd ID)  
                const int rightId = connector * 2 + 1;
                auto rightButton = std::make_unique<PushButton>(rightId, MCP_SWITCH_RIGHT, mcp);
                buttonManager_.addButton(std::move(rightButton));
                buttonCount++;
                
                ESP_LOGI(TAG, "Created buttons %d and %d for connector %d", 
                         leftId + 1, rightId + 1, connector);
            }
        }
    }
    
    ESP_LOGI(TAG, "Button initialization completed. Created %d buttons", buttonCount);
    return buttonCount;
}

bool HardwareInitializer::initializeDisplays() const {
    ESP_LOGI(TAG, "Initializing displays...");

    // Initialize display subsystem with detected hardware
    bool success = FreeButton::initializeDisplays(mcps_, tft320Bar_);

    if (success) {
        // Show hello world and display numbers
        //auto& displayManager = FreeButton::getDisplayManager();
        //displayManager.showHelloWorld();
        //displayManager.showDisplayNumbers();
        //displayManager.showRotationTestPattern();
    }

    ESP_LOGI(TAG, "Display initialization completed");
    return success;
}

bool HardwareInitializer::initializeSensors() {
    ESP_LOGI(TAG, "Initializing sensors...");

    // Initialize sensor subsystem
    FreeButton::initializeSensors();

    ESP_LOGI(TAG, "Sensor initialization completed");
    return true;
}

void HardwareInitializer::onDeviceDetected(const I2CDevice& device) {
    ESP_LOGD(TAG, "Device detected: 0x%02X - %s", device.address, device.description.c_str());
}

void HardwareInitializer::onConnectorDetected(const int connector, const ConnectorType type) {
    ESP_LOGD(TAG, "Connector %d: %s", connector,
             type == ConnectorType::LARGE ? "Large display" : "Mini displays");
    
    // Track TFT320 bar location
    if (type == ConnectorType::LARGE) {
        tft320Bar_ = connector;
    }
}

std::shared_ptr<Adafruit_MCP23008> HardwareInitializer::getMCPForConnector(const int connector) {
    if (connector >= 0 && connector < static_cast<int>(mcps_.size())) {
        return mcps_[connector];
    }
    return nullptr;
}

bool HardwareInitializer::initializeDeviceInfoButton() {
    ESP_LOGI(TAG, "Initializing device info button...");

    // Initialize device info button with CONFIG_BUTTON pin
    if (!FreeButton::initializeDeviceInfoButton(CONFIG_BUTTON)) {
        ESP_LOGE(TAG, "Failed to initialize device info button");
        return false;
    }

    // Start the device info button monitoring
    FreeButton::startDeviceInfoButton();

    ESP_LOGI(TAG, "Device info button initialized successfully");
    return true;
}

// Global convenience functions
bool initializeAllHardware() {
    if (!g_hardwareInitializer) {
        g_hardwareInitializer = std::make_unique<HardwareInitializer>();
    }
    
    return g_hardwareInitializer->initializeHardware();
}

HardwareInitializer& getHardwareInitializer() {
    if (!g_hardwareInitializer) {
        g_hardwareInitializer = std::make_unique<HardwareInitializer>();
    }
    
    return *g_hardwareInitializer;
}

} // namespace FreeButton