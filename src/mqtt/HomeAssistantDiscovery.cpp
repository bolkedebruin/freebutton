//
// Home Assistant Discovery Support
// Created by Bolke de Bruin on 24/08/2025.
//

#include "HomeAssistantDiscovery.h"

#include <atomic>
#include <ranges>

#include "Component.h"
#include "HaCompact.h"
#include "HaSlug.h"
#include "Registry.h"
#include "config.h"
#include "esp_log.h"
#include "esp_random.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

static auto TAG = "ha_discovery";

namespace {

// Listens to Home Assistant's birth message (<prefix>/status). On "online",
// all discovery is republished after a random 0-5 s delay so a fleet of
// devices does not hit Home Assistant at the same instant.
class HaStatusSubscriber final : public MqttSubscriber {
public:
    void onMessage(const uint8_t brokerId, std::string_view topic, const std::string_view data) override {
        if (data == "online") {
            const uint32_t delayMs = esp_random() % 5001;
            ESP_LOGI(TAG, "Home Assistant online on broker %d, republishing discovery in %lu ms",
                     brokerId, static_cast<unsigned long>(delayMs));
            HomeAssistantDiscovery::scheduleRepublish(brokerId, delayMs);
        }
    }
    const std::string getComponentType() const override { return "ha_status"; }
};

HaStatusSubscriber& statusSubscriber() {
    static HaStatusSubscriber subscriber;
    return subscriber;
}

// One bit per broker id: a republish task is pending.
std::atomic<uint32_t> pendingRepublish{0};

// Guards broker_configs_ (registration maps): the republish task walks them
// while main() may still be registering. Recursive: publishAllDiscovery holds
// it and calls helpers that take it again.
SemaphoreHandle_t discoveryMutex() {
    static SemaphoreHandle_t m = xSemaphoreCreateRecursiveMutex();
    return m;
}

class DiscoveryLock {
public:
    DiscoveryLock() { xSemaphoreTakeRecursive(discoveryMutex(), portMAX_DELAY); }
    ~DiscoveryLock() { xSemaphoreGiveRecursive(discoveryMutex()); }
    DiscoveryLock(const DiscoveryLock&) = delete;
    DiscoveryLock& operator=(const DiscoveryLock&) = delete;
};

// Pacing of the discovery burst. This firmware runs small lwIP/MQTT buffers
// (TCP send buffer 2880 B, MQTT buffer 512 B). Publishing ~40 QoS 1 configs
// back to back from the MQTT task stalled a socket write past esp-mqtt's 10 s
// network timeout and dropped the connection. After each config, wait until
// the outbox (unacknowledged QoS 1 data) is below kOutboxLowWaterBytes.
constexpr int kOutboxLowWaterBytes = 1024;
constexpr uint32_t kPaceTimeoutMs = 2000;
constexpr uint32_t kPacePollMs = 10;

// Must NOT run in the MQTT task: the MQTT task is what drains the outbox.
void paceAfterPublish(const MqttBroker& broker) {
    const TickType_t start = xTaskGetTickCount();
    while (broker.isConnected() && broker.outboxSize() > kOutboxLowWaterBytes &&
           (xTaskGetTickCount() - start) < pdMS_TO_TICKS(kPaceTimeoutMs)) {
        vTaskDelay(pdMS_TO_TICKS(kPacePollMs));
    }
}

struct RepublishArgs {
    uint8_t brokerId;
    uint32_t delayMs;
};

void republishTask(void* param) {
    const auto* args = static_cast<RepublishArgs*>(param);
    const uint8_t brokerId = args->brokerId;
    const uint32_t delayMs = args->delayMs;
    delete args;

    vTaskDelay(pdMS_TO_TICKS(delayMs));
    // Clear first: a birth message arriving while we publish schedules another round.
    pendingRepublish.fetch_and(~(1u << brokerId));
    HomeAssistantDiscovery::publishAllDiscovery(brokerId);
    vTaskDelete(nullptr);
}

} // namespace

// Static member definitions
std::map<uint8_t, HomeAssistantDiscovery::BrokerDiscoveryConfig> HomeAssistantDiscovery::broker_configs_;
HomeAssistantDevice HomeAssistantDiscovery::global_device_info_;

void HomeAssistantDiscovery::configureBroker(const uint8_t brokerId, const bool enabled, const std::string& discovery_prefix) {
    DiscoveryLock lock;
    auto& config = broker_configs_[brokerId];
    config.enabled = enabled;
    config.discovery_prefix = discovery_prefix;

    if (enabled) {
        statusSubscriber().addSubscription(brokerId, discovery_prefix + "/status", 1);
    }

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
    DiscoveryLock lock;
    if (!isDiscoveryEnabled(brokerId)) {
        return;
    }

    const auto it = broker_configs_.find(brokerId);
    if (it == broker_configs_.end()) {
        return;
    }

    ESP_LOGI(TAG, "Publishing all discovery configs for broker %d (%zu components, %zu triggers)",
             brokerId, it->second.callbacks.size(), it->second.triggers.size());

    // Publish component discoveries
    for (const auto &callback_info: it->second.callbacks | std::views::values) {
        if (callback_info.enabled) {
            publishCallbackDiscovery(brokerId, callback_info);
        }
    }

    // Publish trigger discoveries
    for (const auto &trigger_info: it->second.triggers | std::views::values) {
        if (trigger_info.enabled) {
            publishTriggerDiscovery(brokerId, trigger_info);
        }
    }
}

uint16_t HomeAssistantDiscovery::registerComponent(uint8_t brokerId, DiscoveryConfigGenerator generator) {
    DiscoveryLock lock;
    auto& broker_config = broker_configs_[brokerId];
    const uint16_t component_id = broker_config.next_component_id++;

    // Store the callback - much more memory efficient
    DiscoveryCallbackInfo callback_info;
    callback_info.generator = std::move(generator);
    callback_info.enabled = true;

    broker_config.callbacks[component_id] = std::move(callback_info);

    // Generate config once to get object_id for logging
    auto config = broker_config.callbacks[component_id].generator(brokerId);
    ESP_LOGD(TAG, "Registered callback component %d (%s/%s) for broker %d",
             component_id, config.component_type.c_str(), config.object_id.c_str(), brokerId);

    // Publish via the coalesced, paced republish task rather than inline:
    // many components register in a row, possibly right after connect.
    if (broker_config.enabled) {
        scheduleRepublish(brokerId, 300);
    }

    return component_id;
}

void HomeAssistantDiscovery::unregisterComponent(const uint8_t brokerId, const uint16_t component_id) {
    DiscoveryLock lock;
    auto& broker_config = broker_configs_[brokerId];
    const auto it = broker_config.callbacks.find(component_id);

    if (it != broker_config.callbacks.end()) {
        // Generate config to get component_type and object_id for removal
        auto config = it->second.generator(brokerId);

        // Remove discovery from HA before unregistering
        removeDiscovery(brokerId, config.component_type, config.object_id);
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
    // Runs in the MQTT task (MQTT_EVENT_CONNECTED). Publishing the whole burst
    // here would block the task that processes PUBACKs and drains the outbox;
    // hand it to the paced republish task instead.
    ESP_LOGI(TAG, "Broker %d connected, scheduling discovery publish", brokerId);
    scheduleRepublish(brokerId, 200);
}

void HomeAssistantDiscovery::scheduleRepublish(const uint8_t brokerId, const uint32_t delayMs) {
    if (brokerId >= 32) {
        ESP_LOGW(TAG, "Broker id %d out of range for republish", brokerId);
        return;
    }
    const uint32_t bit = 1u << brokerId;
    if (pendingRepublish.fetch_or(bit) & bit) {
        ESP_LOGD(TAG, "Discovery republish already pending for broker %d", brokerId);
        return;
    }

    auto* args = new RepublishArgs{brokerId, delayMs};
    if (xTaskCreate(republishTask, "ha_republish", 6144, args, 3, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create discovery republish task for broker %d", brokerId);
        delete args;
        pendingRepublish.fetch_and(~bit);
    }
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
    
    // Device information - use per-component device info if provided, otherwise global
    const HomeAssistantDevice& device = config.device.name.empty() ? global_device_info_ : config.device;

    // Required fields
    doc["name"] = config.name;
    if (!config.object_id.empty()) {
        // Home Assistant expects <domain>.<object_id>; a bare object id makes it
        // fall back to <domain>.unnamed_device. The object id part follows the
        // device's friendly name (like Zigbee2MQTT), e.g. "ButtonPlus Garage"
        // -> event.buttonplus_garage_button_7, falling back to the device id
        // when no usable name is set. HA applies it only to NEW entities;
        // unique_id stays device-id based so nothing is duplicated.
        std::string prefix = haSlugify(device.name);
        if (prefix.empty()) {
            prefix = haSlugify(device.identifier);
        }
        doc["default_entity_id"] = std::format("{}.{}_{}", config.component_type, prefix, config.object_id);
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

    if (!config.event_types.empty()) {
        const auto types = doc["event_types"].to<JsonArray>();
        for (const auto& type : config.event_types) {
            types.add(type);
        }
    }

    if (config.availability) {
        // Matches MqttBroker: LWT "offline" and birth "online", both retained.
        doc["availability_topic"] = std::string(BASE_TOPIC) + device.identifier + "/status";
        doc["payload_available"] = "online";
        doc["payload_not_available"] = "offline";
    }

    // Unique id stays <device>_<object_id> (unchanged from earlier firmware so
    // existing entities keep their registry entry) unless explicitly overridden.
    doc["unique_id"] = config.unique_id.empty()
        ? std::format("{}_{}", device.identifier, config.object_id)
        : config.unique_id;

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
    // Abbreviated keys + "~" base topic: same entity in Home Assistant,
    // smaller on the wire (see HaCompact.h).
    serializeJson(freebutton::ha::compactDiscovery(doc), output);
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
    const std::string topic = generateDiscoveryTopic(broker_config.discovery_prefix, config.component_type, config.object_id);

    if (!config.enabled) {
        // Entity must not exist (e.g. key not configured): clear any stale
        // retained config so Home Assistant removes it.
        ESP_LOGD(TAG, "Clearing discovery at %s", topic.c_str());
        broker->publish(topic, "", 1, true);
        paceAfterPublish(*broker);
        return;
    }

    const std::string json_payload = configToJson(config);

    ESP_LOGD(TAG, "Publishing callback discovery to %s", topic.c_str());
    broker->publish(topic, json_payload, 1, true);
    paceAfterPublish(*broker);
}

void HomeAssistantDiscovery::setDeviceName(std::string name) {
    global_device_info_.name = std::move(name);
}

uint16_t HomeAssistantDiscovery::registerTrigger(uint8_t brokerId, TriggerConfigGenerator generator) {
    DiscoveryLock lock;
    auto& broker_config = broker_configs_[brokerId];
    const uint16_t trigger_id = broker_config.next_trigger_id++;

    // Store the callback
    TriggerCallbackInfo trigger_info;
    trigger_info.generator = std::move(generator);
    trigger_info.enabled = true;

    broker_config.triggers[trigger_id] = std::move(trigger_info);

    // Generate config once to get object_id for logging
    auto config = broker_config.triggers[trigger_id].generator(brokerId);
    ESP_LOGD(TAG, "Registered trigger %d (%s) for broker %d",
             trigger_id, config.object_id.c_str(), brokerId);

    // Publish via the coalesced, paced republish task (see registerComponent).
    if (broker_config.enabled) {
        scheduleRepublish(brokerId, 300);
    }

    return trigger_id;
}

void HomeAssistantDiscovery::unregisterTrigger(const uint8_t brokerId, const uint16_t trigger_id) {
    DiscoveryLock lock;
    auto& broker_config = broker_configs_[brokerId];
    const auto it = broker_config.triggers.find(trigger_id);

    if (it != broker_config.triggers.end()) {
        // Generate config to get object_id for removal
        auto config = it->second.generator(brokerId);

        // Remove discovery from HA before unregistering
        const std::string topic = generateTriggerTopic(broker_config.discovery_prefix,
                                                      global_device_info_.identifier,
                                                      config.object_id);

        auto* broker = MqttBrokerRegistry::getBroker(brokerId);
        if (broker) {
            ESP_LOGD(TAG, "Removing trigger discovery from %s", topic.c_str());
            broker->publish(topic, "", 1, true);  // Empty payload removes the discovery
        }

        broker_config.triggers.erase(it);
        ESP_LOGD(TAG, "Unregistered trigger %d from broker %d", trigger_id, brokerId);
    }
}

std::string HomeAssistantDiscovery::generateTriggerTopic(const std::string& discovery_prefix,
                                                         const std::string& device_id,
                                                         const std::string& object_id) {
    if (device_id.empty()) {
        ESP_LOGW(TAG, "Device identifier not set, cannot generate trigger topic");
        return "";
    }

    return discovery_prefix + "/device_automation/" + device_id + "/" + object_id + "/config";
}

std::string HomeAssistantDiscovery::triggerToJson(const HomeAssistantTriggerConfig& config) {
    JsonDocument doc;

    // Required fields for device automation trigger
    doc["automation_type"] = "trigger";
    doc["type"] = config.type;
    doc["subtype"] = config.subtype;
    doc["topic"] = config.topic;

    // Optional payload filter
    if (!config.payload.empty()) {
        doc["payload"] = config.payload;
    }

    // Device information - use per-trigger device info if provided, otherwise global
    const HomeAssistantDevice& device = config.device.name.empty() ? global_device_info_ : config.device;

    const auto name = !device.name.empty() ? device.name : device.identifier;

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

void HomeAssistantDiscovery::publishTriggerDiscovery(uint8_t brokerId, const TriggerCallbackInfo &trigger_info) {
    if (!isDiscoveryEnabled(brokerId) || !trigger_info.enabled) {
        return;
    }

    auto* broker = MqttBrokerRegistry::getBroker(brokerId);
    if (!broker) {
        ESP_LOGE(TAG, "Broker %d not found", brokerId);
        return;
    }

    // Only publish if broker is connected
    if (!broker->isConnected()) {
        ESP_LOGD(TAG, "Broker %d not connected, trigger discovery will be published on connection", brokerId);
        return;
    }

    // Generate config on-demand
    auto config = trigger_info.generator(brokerId);

    const auto& broker_config = broker_configs_[brokerId];
    const std::string topic = generateTriggerTopic(broker_config.discovery_prefix,
                                                   global_device_info_.identifier,
                                                   config.object_id);

    if (!config.enabled) {
        ESP_LOGD(TAG, "Clearing trigger discovery at %s", topic.c_str());
        broker->publish(topic, "", 1, true);
        paceAfterPublish(*broker);
        return;
    }

    const std::string json_payload = triggerToJson(config);

    ESP_LOGD(TAG, "Publishing trigger discovery to %s", topic.c_str());
    broker->publish(topic, json_payload, 1, true);
    paceAfterPublish(*broker);
}
