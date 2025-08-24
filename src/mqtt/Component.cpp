//
// Created by Bolke de Bruin on 24/08/2025.
//
#include <algorithm>
#include <esp_log.h>

#include "Component.h"
#include "Registry.h"

static const char* TAG = "mqtt_component";

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

void MqttSubscriber::removeSubscription(uint8_t brokerId, const std::string &topic) {
    if (topic.empty()) {
        ESP_LOGE(TAG, "Cannot remove subscription with empty topic");
        return;
    }

    // Find and remove the subscription
    auto it = std::find_if(subscriptions.begin(), subscriptions.end(),
                           [brokerId, &topic](const TopicSubscription& sub) {
                               return sub.brokerId == brokerId && sub.topic == topic;
                           });

    if (it != subscriptions.end()) {
        subscriptions.erase(it);
        ESP_LOGI(TAG, "Removed subscription to topic %s on broker %d", topic.c_str(), brokerId);

        // Always inform the broker to remove this subscriber from the topic
        // The broker will handle cleanup if this was the last subscriber
        auto &registry = MqttBrokerRegistry::instance();
        if (auto* broker = registry.getBroker(brokerId)) {
            broker->unsubscribe(this);
        }
    } else {
        ESP_LOGW(TAG, "Subscription to topic %s on broker %d not found", topic.c_str(), brokerId);
    }
};


