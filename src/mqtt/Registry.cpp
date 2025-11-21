//
// Created by Bolke de Bruin on 24/08/2025.
//

#include "Registry.h"
#include "esp_log.h"

static const char* TAG = "broker_registry";

MqttBrokerRegistry& MqttBrokerRegistry::instance() {
    static MqttBrokerRegistry instance;
    return instance;
}

MqttBrokerRegistry::~MqttBrokerRegistry() {
    disconnectAll();
}

uint8_t MqttBrokerRegistry::addBroker(std::unique_ptr<MqttBroker> broker) {
    if (!broker) {
        ESP_LOGE(TAG, "Cannot add null broker");
        return 255; // Indicate error
    }

    if (next_id == 255) {
        ESP_LOGE(TAG, "Maximum number of brokers reached");
        return 255; // Indicate error
    }

    auto name = broker->getName();
    if (getBroker(name) != nullptr) {
        ESP_LOGE(TAG, "Broker with name %.*s already exists", name.length(), name.data());
        return 255; // Indicate error
    }

    uint8_t id = next_id++;

    ESP_LOGI(TAG, "Adding broker %d: %.*s", id, name.length(), name.data());
    broker->setBrokerId(id);

    brokers.push_back(std::move(broker));
    return id;

}

MqttBroker* MqttBrokerRegistry::getBroker(const uint8_t brokerId) {
    if (brokerId >= brokers.size()) {
        ESP_LOGW(TAG, "Invalid broker ID: %d", brokerId);
        return nullptr;
    }

    return brokers[brokerId].get();
}

uint8_t MqttBrokerRegistry::getBrokerId(std::string_view name) {
    if (name.empty()) {
        return 255;
    }

    for (size_t i = 0; i < brokers.size(); ++i) {
        if (namesMatch(brokers[i]->getName(), name)) {
            return static_cast<uint8_t>(i);
        }
    }

    return 255; // Not found - calling function can log warning if needed
}

MqttBroker* MqttBrokerRegistry::getBroker(std::string_view name) {
    if (name.empty()) {
        return nullptr;
    }

    for (const auto& broker : brokers) {
        if (namesMatch(broker->getName(), name)) {
            return broker.get();
        }
    }

    return nullptr; // Not found - calling function can log warning if needed
}

uint8_t MqttBrokerRegistry::getOrCreateBrokerId(std::string_view name) {
    if (name.empty()) {
        ESP_LOGW(TAG, "Cannot get or create ID for empty name");
        return 255;
    }

    // Silent lookup - don't log warnings for missing brokers during auto-registration
    for (size_t i = 0; i < brokers.size(); ++i) {
        if (namesMatch(brokers[i]->getName(), name)) {
            return static_cast<uint8_t>(i);
        }
    }

    // Auto-register placeholder broker (will be configured later)
    ESP_LOGI(TAG, "Auto-registering placeholder broker: %.*s", (int)name.length(), name.data());
    auto broker = std::make_unique<MqttBroker>(std::string(name), "");
    return instance().addBroker(std::move(broker));
}


void MqttBrokerRegistry::connectAll() {
    ESP_LOGI(TAG, "Connecting to all %d brokers", brokers.size());

    for (auto& broker : brokers) {
        broker->start();
    }
}

void MqttBrokerRegistry::disconnectAll() {
    ESP_LOGI(TAG, "Disconnecting from all brokers");

    for (auto& broker : brokers) {
        broker->stop();
    }
}
