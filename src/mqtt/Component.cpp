//
// Created by Bolke de Bruin on 24/08/2025.
//
#include <algorithm>
#include <esp_log.h>

#include "Component.h"
#include "Registry.h"

static auto TAG = "mqtt_component";

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

    if (auto* broker = MqttBrokerRegistry::getBroker(brokerId)) {
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
    auto it = std::ranges::find_if(subscriptions,
                                   [brokerId, &topic](const TopicSubscription& sub) {
                                       return sub.brokerId == brokerId && sub.topic == topic;
                                   });

    if (it != subscriptions.end()) {
        subscriptions.erase(it);
        ESP_LOGI(TAG, "Removed subscription to topic %s on broker %d", topic.c_str(), brokerId);

        // Always inform the broker to remove this subscriber from the topic
        // The broker will handle cleanup if this was the last subscriber
        if (auto* broker = MqttBrokerRegistry::getBroker(brokerId)) {
            broker->unsubscribe(this);
        }
    } else {
        ESP_LOGW(TAG, "Subscription to topic %s on broker %d not found", topic.c_str(), brokerId);
    }
};

void MqttSubscriber::clearSubscriptions() {
    // Make a copy to avoid iterator invalidation during removal
    auto currentSubscriptions = subscriptions;
    for (const auto& sub : currentSubscriptions) {
        removeSubscription(sub.brokerId, sub.topic);
    }
    ESP_LOGD(TAG, "Cleared all subscriptions");
}

std::vector<std::string> MqttSubscriber::getSubscriptionTopics(uint8_t brokerId) const {
    std::vector<std::string> topics;
    for (const auto& sub : subscriptions) {
        if (sub.brokerId == brokerId) {
            topics.push_back(sub.topic);
        }
    }
    return topics;
}

void MqttPublisher::addPublication(uint8_t brokerId, const std::string &topic, int qos, bool retain) {
    if (topic.empty()) {
        ESP_LOGE(TAG, "Cannot add publication with empty topic");
        return;
    }

    // check for existing publication
    for (const auto &pub : publications) {
        if (pub.brokerId == brokerId && pub.topic == topic) {
            ESP_LOGW(TAG, "Publication to topic %s on broker %d already exists", topic.c_str(), brokerId);
            return;
        }
    }

    publications.emplace_back(brokerId, topic, qos, retain);
}

void MqttPublisher::removePublication(uint8_t brokerId, const std::string &topic) {
    if (topic.empty()) {
        ESP_LOGE(TAG, "Cannot remove publication with empty topic");
        return;
    }

    auto it = std::ranges::find_if(publications,
                                   [brokerId, &topic](const TopicPublication& pub) {
                                       return pub.brokerId == brokerId && pub.topic == topic;
                                   });

    if (it != publications.end()) {
        publications.erase(it);
        ESP_LOGI(TAG, "Removed publication to topic %s on broker %d", topic.c_str(), brokerId);
    } else {
        ESP_LOGW(TAG, "Publication to topic %s on broker %d not found", topic.c_str(), brokerId);
    }
}

void MqttPublisher::clearPublications() {
    // Make a copy to avoid iterator invalidation during removal
    auto currentPublications = publications;
    for (const auto& pub : currentPublications) {
        removePublication(pub.brokerId, pub.topic);
    }
    ESP_LOGD(TAG, "Cleared all publications");
}

std::vector<std::string> MqttPublisher::getPublicationTopics(uint8_t brokerId) const {
    std::vector<std::string> topics;
    for (const auto& pub : publications) {
        if (pub.brokerId == brokerId) {
            topics.push_back(pub.topic);
        }
    }
    return topics;
}

void MqttPublisher::publish(const std::string &data, int qos, bool retain_override) const {
    // Publish to all registered publications
    for (const auto& pub : publications) {
        if (auto* broker = MqttBrokerRegistry::getBroker(pub.brokerId)) {
            int actual_qos = (qos == -1) ? pub.qos : qos;
            bool actual_retain = retain_override ? true : pub.retain;
            broker->publish(pub.topic, data, actual_qos, actual_retain ? 1 : 0);
            ESP_LOGD(TAG, "Published to topic %s on broker %d", pub.topic.c_str(), pub.brokerId);
        } else {
            ESP_LOGE(TAG, "Broker with ID %d not found, cannot publish to topic %s", pub.brokerId, pub.topic.c_str());
        }
    }
}

void MqttPublisher::publish(uint8_t brokerId, const std::string &topic, const std::string &data, int qos, bool retain) {
    if (topic.empty()) {
        ESP_LOGE(TAG, "Cannot publish to empty topic");
        return;
    }

    if (auto* broker = MqttBrokerRegistry::getBroker(brokerId)) {
        broker->publish(topic, data, qos, retain ? 1 : 0);
        ESP_LOGD(TAG, "Published to topic %s on broker %d", topic.c_str(), brokerId);
    } else {
        ESP_LOGE(TAG, "Broker with ID %d not found, cannot publish to topic %s", brokerId, topic.c_str());
    }
}


