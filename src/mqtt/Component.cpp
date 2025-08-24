//
// Created by Bolke de Bruin on 24/08/2025.
//
#include <esp_log.h>

#include "Component.h"
#include "Registry.h"

auto TAG = "mqtt_component";

void MqttSubscriber::addSubscription(uint8_t brokerId, const std::string &topic, int qos) {
    if (topic.empty()) {
        ESP_LOGE(TAG, "Cannot add subscription with empty topic");
        return;
    }

    // check for existing subscription
    for (const auto &sub : subscriptions) {
        if (sub.brokerId == brokerId && sub.topic == topic) {
            ESP_LOGW(TAG, "Subscription to topic %s on broker %d already exists", topic.c_str(), brokerId);
            return;
        }
    }

    subscriptions.emplace_back(brokerId, topic, qos);
    ESP_LOGI(TAG, "Added subscription to topic %s on broker %d", topic.c_str(), brokerId);

    auto &registry = MqttBrokerRegistry::instance();
    auto* broker = registry.getBroker(brokerId);
    if (broker) {
        broker->subscribe(topic, qos, this);
    } else {
        ESP_LOGE(TAG, "Broker with ID %d not found, cannot subscribe to topic %s", brokerId, topic.c_str());
    }
};


