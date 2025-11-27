//
// Home Assistant Discovery Support
// Created by Bolke de Bruin on 24/08/2025.
//

#include "HomeAssistantDiscovery.h"

#include <ranges>

#include "Registry.h"
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

bool HomeAssistantDiscovery::isDiscoveryEnabled(const uint8_t brokerId) {
    const auto it = broker_configs_.find(brokerId);
    return it != broker_configs_.end() && it->second.enabled;
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

    ESP_LOGI(TAG, "Publishing all callback discovery configs for broker %d (%zu components)",
             brokerId, it->second.callbacks.size());

    for (const auto &callback_info: it->second.callbacks | std::views::values) {
        if (callback_info.enabled) {
            publishCallbackDiscovery(brokerId, callback_info);
        }
    }
}

uint16_t HomeAssistantDiscovery::registerComponent(uint8_t brokerId, DiscoveryConfigGenerator generator) {
    auto& broker_config = broker_configs_[brokerId];
    const uint16_t component_id = broker_config.next_component_id++;

    // Store the callback - much more memory efficient
    DiscoveryCallbackInfo callback_info;
    callback_info.generator = std::move(generator);
    callback_info.enabled = true;

    broker_config.callbacks[component_id] = std::move(callback_info);

    // Generate config once to get default_entity_id for logging
    auto config = broker_config.callbacks[component_id].generator(brokerId);
    ESP_LOGD(TAG, "Registered callback component %d (%s) for broker %d",
             component_id, config.default_entity_id.c_str(), brokerId);

    // Auto-publish if broker is enabled
    if (broker_config.enabled) {
        publishCallbackDiscovery(brokerId, broker_config.callbacks[component_id]);
    }

    return component_id;
}

void HomeAssistantDiscovery::unregisterComponent(const uint8_t brokerId, const uint16_t component_id) {
    auto& broker_config = broker_configs_[brokerId];
    const auto it = broker_config.callbacks.find(component_id);

    if (it != broker_config.callbacks.end()) {
        // Generate config to get component_type and default_entity_id for removal
        auto config = it->second.generator(brokerId);

        // Remove discovery from HA before unregistering
        removeDiscovery(brokerId, config.component_type, config.default_entity_id);
        broker_config.callbacks.erase(it);
        ESP_LOGD(TAG, "Unregistered callback component %d from broker %d", component_id, brokerId);
    }
}

void HomeAssistantDiscovery::setDeviceInfo(const HomeAssistantDevice& device) {
    global_device_info_ = device;
    ESP_LOGI(TAG, "Set global device info: %s (%s)", device.name.c_str(), device.identifier.c_str());
}

const HomeAssistantDevice& HomeAssistantDiscovery::getGlobalDeviceInfo() {
    return global_device_info_;
}

void HomeAssistantDiscovery::onBrokerConnected(uint8_t brokerId) {
    ESP_LOGI(TAG, "Broker %d connected, publishing discovery configs", brokerId);
    publishAllDiscovery(brokerId);
}

std::string HomeAssistantDiscovery::generateDiscoveryTopic(const std::string& discovery_prefix, const std::string& component_type, const std::string& object_id) {
    if (global_device_info_.identifier.empty()) {
        ESP_LOGW(TAG, "Global device identifier(s) not set, cannot generate discovery topic");
        return "";
    }

    return discovery_prefix + "/" + component_type + "/" + global_device_info_.identifier + "/" + object_id + "/config";
}

std::string HomeAssistantDiscovery::configToJson(const HomeAssistantDiscoveryConfig& config) {
    JsonDocument doc;
    
    // Required fields
    doc["name"] = config.name;
    if (!config.default_entity_id.empty()) {
        doc["default_entity_id"] = config.default_entity_id;
    }

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

    // set unique id
    doc["unique_id"] = std::format("{}_{}", device.identifier, config.default_entity_id);

    const auto name = !device.name.empty() ? device.name : device.identifier;;

    const JsonObject deviceObj = doc["device"].to<JsonObject>();
    deviceObj["name"] = name;

    if (!device.manufacturer.empty()) {
        deviceObj["manufacturer"] = device.manufacturer;
    }
    if (!device.model.empty()) {
        deviceObj["model"] = device.model;
    }
    if (!device.sw_version.empty()) {
        deviceObj["sw_version"] = device.sw_version;
    }
    if (!device.identifier.empty()) {
        const auto identifiers = deviceObj["identifiers"].to<JsonArray>();
        identifiers.add(device.identifier);
    }

    // Serialize to string
    std::string output;
    serializeJson(doc, output);
    return output;
}

void HomeAssistantDiscovery::publishCallbackDiscovery(uint8_t brokerId, const DiscoveryCallbackInfo &callback_info) {
    if (!isDiscoveryEnabled(brokerId) || !callback_info.enabled) {
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

    // Generate config on-demand
    auto config = callback_info.generator(brokerId);

    const auto& broker_config = broker_configs_[brokerId];
    const std::string topic = generateDiscoveryTopic(broker_config.discovery_prefix, config.component_type, config.default_entity_id);
    const std::string json_payload = configToJson(config);

    ESP_LOGD(TAG, "Publishing callback discovery to %s", topic.c_str());
    broker->publish(topic, json_payload, 1, true);
}

void HomeAssistantDiscovery::setDeviceName(std::string name) {
    global_device_info_.name = std::move(name);
}
