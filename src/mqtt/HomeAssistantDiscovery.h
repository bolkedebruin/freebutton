//
// Home Assistant Discovery Support
// Created by Bolke de Bruin on 24/08/2025.
//

#ifndef FREEBUTTON_HOMEASSISTANTDISCOVERY_H
#define FREEBUTTON_HOMEASSISTANTDISCOVERY_H

#include <string>
#include <map>
#include <memory>
#include <functional>
#include <ArduinoJson.h>

// Forward declarations
class MqttBroker;
class MqttComponent;

struct HomeAssistantDevice {
    std::string name;
    std::string manufacturer;
    std::string model;
    std::string sw_version;
    std::string identifiers;  // Unique device identifier
};

struct HomeAssistantDiscoveryConfig {
    std::string component_type;    // sensor, binary_sensor, switch, etc.
    std::string object_id;         // Unique object ID
    std::string name;              // Human-readable name
    std::string device_class;      // Home Assistant device class
    std::string state_topic;       // MQTT topic for state (publishers)
    std::string command_topic;     // MQTT topic for commands (subscribers)
    std::string unit_of_measurement;
    std::string value_template;    // Optional JSON value template
    std::map<std::string, std::string> additional_config; // Any additional HA config
    HomeAssistantDevice device;    // Device information
    bool enabled = true;           // Per-component discovery enable/disable
};

class HomeAssistantDiscovery {
public:
    // Configure discovery for a specific broker
    static void configureBroker(uint8_t brokerId, bool enabled = true, 
                              const std::string& discovery_prefix = "homeassistant");
    
    // Check if discovery is enabled for a broker
    static bool isDiscoveryEnabled(uint8_t brokerId);
    
    // Publish discovery configuration for a component
    static void publishDiscovery(uint8_t brokerId, const HomeAssistantDiscoveryConfig& config);
    
    // Remove discovery configuration for a component
    static void removeDiscovery(uint8_t brokerId, const std::string& component_type, 
                              const std::string& object_id);
    
    // Publish discovery for all registered components on a broker
    static void publishAllDiscovery(uint8_t brokerId);
    
    // Register a component for discovery
    static uint16_t registerComponent(uint8_t brokerId, const HomeAssistantDiscoveryConfig& config);
    
    // Register an MqttComponent for discovery (auto-detects topics)
    static uint16_t registerComponent(uint8_t brokerId, MqttComponent* component, 
                                    const std::string& component_type,
                                    const std::string& object_id,
                                    const std::string& name,
                                    const std::string& device_class = "");
    
    // Unregister a component from discovery
    static void unregisterComponent(uint8_t brokerId, uint16_t component_id);
    
    // Set global device information (used for all components)
    static void setDeviceInfo(const HomeAssistantDevice& device);
    
    // Callback for when a broker connects (auto-publishes discovery if enabled)
    static void onBrokerConnected(uint8_t brokerId);

private:
    struct BrokerDiscoveryConfig {
        bool enabled = false;
        std::string discovery_prefix = "homeassistant";
        std::map<uint16_t, HomeAssistantDiscoveryConfig> components; // Use numeric IDs
        uint16_t next_component_id = 1; // Start from 1, 0 reserved for invalid
    };
    
    static std::map<uint8_t, BrokerDiscoveryConfig> broker_configs_;
    static HomeAssistantDevice global_device_info_;
    
    // Generate the discovery topic for a component
    static std::string generateDiscoveryTopic(const std::string& discovery_prefix,
                                            const std::string& component_type,
                                            const std::string& object_id);
    
    // Convert config to JSON string
    static std::string configToJson(const HomeAssistantDiscoveryConfig& config);
};

#endif //FREEBUTTON_HOMEASSISTANTDISCOVERY_H