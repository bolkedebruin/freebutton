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
    std::string identifier;  // Unique device identifier
};

struct HomeAssistantDiscoveryConfig {
    std::string component_type;    // sensor, binary_sensor, switch, etc.
    std::string default_entity_id; // Home Assistant default entity ID
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

// Device automation trigger configuration (separate from entity discovery)
struct HomeAssistantTriggerConfig {
    std::string object_id;         // Unique object ID for this trigger
    std::string type;              // button_short_press, button_long_press, etc.
    std::string subtype;           // button_1, button_2, etc.
    std::string topic;             // MQTT topic to listen for trigger events
    std::string payload;           // Optional payload to match (e.g., "{\"event_type\":\"click\"}")
    HomeAssistantDevice device;    // Device information
    bool enabled = true;           // Per-trigger enable/disable
};

// Callback function type for on-demand config generation
using DiscoveryConfigGenerator = std::function<HomeAssistantDiscoveryConfig(uint8_t brokerId)>;
using TriggerConfigGenerator = std::function<HomeAssistantTriggerConfig(uint8_t brokerId)>;

// Lightweight callback storage - replaces heavy config storage
struct DiscoveryCallbackInfo {
    DiscoveryConfigGenerator generator;
    bool enabled = true;
    // Memory footprint: ~24 bytes vs 670 bytes for stored HomeAssistantDiscoveryConfig
};

struct TriggerCallbackInfo {
    TriggerConfigGenerator generator;
    bool enabled = true;
};

class HomeAssistantDiscovery {
public:
    // Configure discovery for a specific broker
    static void configureBroker(uint8_t brokerId, bool enabled = true, 
                              const std::string& discovery_prefix = "homeassistant");
    
    // Check if discovery is enabled for a broker
    static bool isDiscoveryEnabled(uint8_t brokerId);
    
    // Remove discovery configuration for a component
    
    // Remove discovery configuration for a component
    static void removeDiscovery(uint8_t brokerId, const std::string& component_type, 
                              const std::string& object_id);
    
    // Publish discovery for all registered components on a broker
    static void publishAllDiscovery(uint8_t brokerId);
    
    // NEW CALLBACK-BASED API - Memory efficient discovery registration
    static uint16_t registerComponent(uint8_t brokerId, DiscoveryConfigGenerator generator);

    // Unregister a component from discovery
    static void unregisterComponent(uint8_t brokerId, uint16_t component_id);

    // Device automation trigger registration
    static uint16_t registerTrigger(uint8_t brokerId, TriggerConfigGenerator generator);

    // Unregister a trigger from discovery
    static void unregisterTrigger(uint8_t brokerId, uint16_t trigger_id);

    // Set global device information (used for all components)
    static void setDeviceInfo(const HomeAssistantDevice& device);

    // Get global device information (used by callbacks)
    static const HomeAssistantDevice& getGlobalDeviceInfo();
    
    // Callback for when a broker connects (auto-publishes discovery if enabled)
    static void onBrokerConnected(uint8_t brokerId);

    static void setDeviceName(const std::string name);

private:
    struct BrokerDiscoveryConfig {
        bool enabled = false;
        std::string discovery_prefix = "homeassistant";
        std::map<uint16_t, DiscoveryCallbackInfo> callbacks; // Memory-efficient callbacks only
        std::map<uint16_t, TriggerCallbackInfo> triggers;    // Device automation triggers
        uint16_t next_component_id = 1; // Start from 1, 0 reserved for invalid
        uint16_t next_trigger_id = 1;   // Start from 1, 0 reserved for invalid
    };

    // Helper to publish single callback-based discovery config
    static void publishCallbackDiscovery(uint8_t brokerId, const DiscoveryCallbackInfo &callback_info);

    // Helper to publish single trigger discovery config
    static void publishTriggerDiscovery(uint8_t brokerId, const TriggerCallbackInfo &trigger_info);

    static std::map<uint8_t, BrokerDiscoveryConfig> broker_configs_;
    static HomeAssistantDevice global_device_info_;

    // Generate the discovery topic for a component
    static std::string generateDiscoveryTopic(const std::string& discovery_prefix,
                                            const std::string& component_type,
                                            const std::string& object_id);

    // Generate the discovery topic for a device automation trigger
    static std::string generateTriggerTopic(const std::string& discovery_prefix,
                                           const std::string& device_id,
                                           const std::string& object_id);

    // Convert config to JSON string
    static std::string configToJson(const HomeAssistantDiscoveryConfig& config);

    // Convert trigger config to JSON string
    static std::string triggerToJson(const HomeAssistantTriggerConfig& config);
};

#endif //FREEBUTTON_HOMEASSISTANTDISCOVERY_H