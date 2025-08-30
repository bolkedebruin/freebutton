//
// Home Assistant Discovery Support
// Created by Bolke de Bruin on 24/08/2025.
//

#include "HomeAssistantDiscovery.h"
#include "Registry.h"
#include "Component.h"
#include "esp_log.h"

static auto TAG = "ha_discovery";

// Static member definitions
std::map<uint8_t, HomeAssistantDiscovery::BrokerDiscoveryConfig> HomeAssistantDiscovery::broker_configs_;
HomeAssistantDevice HomeAssistantDiscovery::global_device_info_;

void HomeAssistantDiscovery::configureBroker(const uint8_t brokerId, const bool enabled, const std::string& discovery_prefix) {
    auto& config = broker_configs_[brokerId];
    config.enabled = enabled;
    config.discovery_prefix = discovery_prefix;
    
    ESP_LOGI(TAG, "Configured discovery for broker %d: %s (prefix: %s)", 
             brokerId, enabled ? "enabled" : "disabled", discovery_prefix.c_str());
}

bool HomeAssistantDiscovery::isDiscoveryEnabled(uint8_t brokerId) {
    const auto it = broker_configs_.find(brokerId);
    return it != broker_configs_.end() && it->second.enabled;
}

void HomeAssistantDiscovery::publishDiscovery(uint8_t brokerId, const HomeAssistantDiscoveryConfig& config) {
    if (!isDiscoveryEnabled(brokerId)) {
        return;
    }

    auto* broker = MqttBrokerRegistry::getBroker(brokerId);
    if (!broker) {
        ESP_LOGE(TAG, "Broker %d not found", brokerId);
        return;
    }

    // Only publish if broker is connected
    if (!broker->isConnected()) {
        ESP_LOGD(TAG, "Broker %d not connected, discovery will be published on connection", brokerId);
        return;
    }

    const auto& broker_config = broker_configs_[brokerId];
    const std::string topic = generateDiscoveryTopic(broker_config.discovery_prefix, config.component_type, config.object_id);
    const std::string json_payload = configToJson(config);

    ESP_LOGD(TAG, "Publishing discovery to %s", topic.c_str());
    broker->publish(topic, json_payload, 1, true);
}

void HomeAssistantDiscovery::removeDiscovery(uint8_t brokerId, const std::string& component_type, const std::string& object_id) {
    if (!isDiscoveryEnabled(brokerId)) {
        return;
    }

    auto* broker = MqttBrokerRegistry::getBroker(brokerId);
    if (!broker) {
        ESP_LOGE(TAG, "Broker %d not found", brokerId);
        return;
    }

    const auto& broker_config = broker_configs_[brokerId];
    const std::string topic = generateDiscoveryTopic(broker_config.discovery_prefix, component_type, object_id);
    
    ESP_LOGD(TAG, "Removing discovery from %s", topic.c_str());
    broker->publish(topic, "", 1, true);  // Empty payload removes the discovery
}

void HomeAssistantDiscovery::publishAllDiscovery(const uint8_t brokerId) {
    if (!isDiscoveryEnabled(brokerId)) {
        return;
    }

    const auto it = broker_configs_.find(brokerId);
    if (it == broker_configs_.end()) {
        return;
    }

    ESP_LOGI(TAG, "Publishing all discovery configs for broker %d (%zu components)", 
             brokerId, it->second.components.size());

    for (const auto& [component_id, config] : it->second.components) {
        if (config.enabled) {
            publishDiscovery(brokerId, config);
        }
    }
}

uint16_t HomeAssistantDiscovery::registerComponent(uint8_t brokerId, const HomeAssistantDiscoveryConfig& config) {
    auto& broker_config = broker_configs_[brokerId];
    const uint16_t component_id = broker_config.next_component_id++;
    
    // Store the config
    broker_config.components[component_id] = config;
    
    ESP_LOGD(TAG, "Registered component %d (%s) for broker %d", 
             component_id, config.object_id.c_str(), brokerId);
    
    // Auto-publish if broker is enabled and component is enabled
    if (broker_config.enabled && config.enabled) {
        publishDiscovery(brokerId, config);
    }
    
    return component_id;
}

uint16_t HomeAssistantDiscovery::registerComponent(uint8_t brokerId, MqttComponent* component, 
                                                  const std::string& component_type,
                                                  const std::string& object_id,
                                                  const std::string& name,
                                                  const std::string& device_class) {
    if (!component) {
        ESP_LOGE(TAG, "Cannot register null component");
        return 0; // Invalid component ID
    }

    HomeAssistantDiscoveryConfig config;
    config.component_type = component_type;
    config.object_id = object_id;
    config.name = name;
    config.device_class = device_class;

    // Auto-detect topics using virtual methods (no RTTI required)
    auto command_topics = component->getSubscriptionTopics(brokerId);
    auto state_topics = component->getPublicationTopics(brokerId);

    // Set command topic if component has subscriptions
    if (!command_topics.empty()) {
        config.command_topic = command_topics[0]; // Use first topic
    }

    // Set state topic if component has publications  
    if (!state_topics.empty()) {
        config.state_topic = state_topics[0]; // Use first topic
    }

    // Components can have both subscription and publication topics
    // This is common for switches, lights, etc.

    ESP_LOGD(TAG, "Auto-registered component %s: state_topic=%s, command_topic=%s", 
             object_id.c_str(), config.state_topic.c_str(), config.command_topic.c_str());

    return registerComponent(brokerId, config);
}

void HomeAssistantDiscovery::unregisterComponent(uint8_t brokerId, uint16_t component_id) {
    auto& broker_config = broker_configs_[brokerId];
    const auto it = broker_config.components.find(component_id);
    
    if (it != broker_config.components.end()) {
        // Remove discovery from HA before unregistering
        removeDiscovery(brokerId, it->second.component_type, it->second.object_id);
        broker_config.components.erase(it);
        ESP_LOGD(TAG, "Unregistered component %d from broker %d", component_id, brokerId);
    }
}

void HomeAssistantDiscovery::setDeviceInfo(const HomeAssistantDevice& device) {
    global_device_info_ = device;
    ESP_LOGI(TAG, "Set global device info: %s (%s)", device.name.c_str(), device.identifiers.c_str());
}

void HomeAssistantDiscovery::onBrokerConnected(uint8_t brokerId) {
    ESP_LOGI(TAG, "Broker %d connected, publishing discovery configs", brokerId);
    publishAllDiscovery(brokerId);
}

std::string HomeAssistantDiscovery::generateDiscoveryTopic(const std::string& discovery_prefix, const std::string& component_type, const std::string& object_id) {
    if (global_device_info_.identifiers.empty()) {
        ESP_LOGW(TAG, "Global device identifier(s) not set, cannot generate discovery topic");
        return "";
    }

    return discovery_prefix + "/" + component_type + "/" + global_device_info_.identifiers + "/" + object_id + "/config";
}

std::string HomeAssistantDiscovery::configToJson(const HomeAssistantDiscoveryConfig& config) {
    JsonDocument doc;
    
    // Required fields
    doc["name"] = config.name;
    doc["object_id"] = config.object_id;
    
    // State topic (for publishers/sensors)
    if (!config.state_topic.empty()) {
        doc["state_topic"] = config.state_topic;
    }
    
    // Command topic (for subscribers/switches)
    if (!config.command_topic.empty()) {
        doc["command_topic"] = config.command_topic;
    }
    
    // Optional fields - only add if not empty
    if (!config.device_class.empty()) {
        doc["device_class"] = config.device_class;
    }
    
    if (!config.unit_of_measurement.empty()) {
        doc["unit_of_measurement"] = config.unit_of_measurement;
    }
    
    if (!config.value_template.empty()) {
        doc["value_template"] = config.value_template;
    }
    
    // Add additional config parameters
    for (const auto& [key, value] : config.additional_config) {
        doc[key] = value;
    }
    
    // Device information - use per-component device info if provided, otherwise global
    const HomeAssistantDevice& device = config.device.name.empty() ? global_device_info_ : config.device;
    
    if (!device.name.empty()) {
        const JsonObject deviceObj = doc["device"].to<JsonObject>();
        deviceObj["name"] = device.name;
        
        if (!device.manufacturer.empty()) {
            deviceObj["manufacturer"] = device.manufacturer;
        }
        if (!device.model.empty()) {
            deviceObj["model"] = device.model;
        }
        if (!device.sw_version.empty()) {
            deviceObj["sw_version"] = device.sw_version;
        }
        if (!device.identifiers.empty()) {
            const auto identifiers = deviceObj["identifiers"].to<JsonArray>();
            identifiers.add(device.identifiers);
        }
    }
    
    // Serialize to string
    std::string output;
    serializeJson(doc, output);
    return output;
}