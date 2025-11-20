//
// Created by Bolke de Bruin on 10/08/2025.
//
#include "Broker.h"

#include <algorithm>
#include <cstring>

#include "esp_log.h"

static const auto TAG = "mqtt";

void MqttBroker::start() {
    std::string lwt_topic = std::string(deviceTopic) + "/status";

    if (url.empty()) {
        ESP_LOGE(TAG, "MQTT URL is empty, cannot start broker");
    }

    std::ranges::transform(url, url.begin(), ::tolower);

    if (url.find("mqtt://") == 0 || url.find("ws://") == 0 ||
        url.find("wss://") == 0 || url.find("mqtts://") == 0) {

        esp_mqtt_client_config_t mqtt_cfg = {};
        mqtt_cfg.broker.address.uri = url.c_str();
        mqtt_cfg.broker.address.port = port > 0 ? port : (url.find("ws://") == 0 || url.find("wss://") == 0 ? 80 : 1883);
        mqtt_cfg.credentials.username = username.c_str();
        mqtt_cfg.credentials.authentication.password = password.c_str();
        mqtt_cfg.session.last_will.topic = lwt_topic.c_str();
        mqtt_cfg.session.last_will.msg = "offline";
        mqtt_cfg.session.last_will.qos = 1;
        mqtt_cfg.session.last_will.retain = 1;

        client = esp_mqtt_client_init(&mqtt_cfg);
        esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, mqtt_event_handler, this);
        if (esp_mqtt_client_start(client) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start MQTT client");
            esp_mqtt_client_destroy(client);
            client = nullptr;
        } else {
            enabled = true;
        }
    }
}

void MqttBroker::stop() {
    if (client) {
        esp_mqtt_client_stop(client);
        esp_mqtt_client_destroy(client);
        client = nullptr;
        enabled = false;
    }
}

void MqttBroker::sendBirth() const {
    const std::string lwt_topic = std::string(deviceTopic) + "/status";
    publish(lwt_topic, "online", 1, 1);
}

void MqttBroker::publish(std::string_view topic, std::string_view data, const int qos, const int retain) const {
    if (topic.empty()) {
        ESP_LOGE(TAG, "Empty topic (data: %.*s)", data.length(), data.data());
        return;
    }

    ESP_LOGI(TAG, "topic: %.*s data: %.*s", topic.length(), topic.data(), data.length(), data.data());

    if (enabled) {
        // ensure null terminated with no heap allocation
        char topic_buf[topic.length() + 1];
        char data_buf[data.length() + 1];

        std::memcpy(topic_buf, topic.data(), topic.length());
        std::memcpy(data_buf, data.data(), data.length());
        topic_buf[topic.length()] = '\0';
        data_buf[data.length()] = '\0';

        if (int ret = esp_mqtt_client_publish(client, topic_buf, data_buf, 0, qos, retain); ret < 0) {
            ESP_LOGW(TAG, "Failed to enqueue message: %d", ret);
        }
    }
}

void MqttBroker::subscribe(std::string_view topic, const int qos, MqttSubscriber *subscriber) {
    if (!client) {
        ESP_LOGE(TAG, "MQTT client not initialized, cannot subscribe to topic: %s", topic.data());
        return;
    }

    if (topic.empty()) {
        ESP_LOGE(TAG, "Empty topic for subscription");
        return;
    }

    ESP_LOGD(TAG, "Subscribing to topic: %.*s", topic.length(), topic.data());

    auto it = std::find_if(subscriptions_.begin(), subscriptions_.end(),
                           [topic](const SubscriptionEntry& entry) { return entry.topic == topic; });

    bool needsBrokerSubscription = false;
    
    if (it != subscriptions_.end()) {
        // Topic already exists, add subscriber if not already present
        if (std::ranges::find(it->subscribers, subscriber) == it->subscribers.end()) {
            it->subscribers.push_back(subscriber);
        } else {
            ESP_LOGW(TAG, "Subscriber already registered for topic: %s", topic.data());
        }
    } else {
        // New topic, create entry and mark for broker subscription
        SubscriptionEntry newEntry;
        newEntry.topic = std::string(topic);
        newEntry.subscribers.push_back(subscriber);
        subscriptions_.push_back(newEntry);
        needsBrokerSubscription = true;
    }
    
    // Only subscribe to broker if this is the first subscriber to this topic and broker is connected
    if (enabled && needsBrokerSubscription) {
        if (int msg_id = esp_mqtt_client_subscribe(client, topic.data(), qos); msg_id < 0) {
            ESP_LOGD(TAG, "Failed to subscribe to topic: %s, error: %d (will retry on reconnect)", topic.data(), msg_id);
        }
    } else if (!enabled && needsBrokerSubscription) {
        ESP_LOGI(TAG, "Broker not connected yet, topic %.*s will be subscribed when connection is established", (int)topic.length(), topic.data());
    }
}

void MqttBroker::unsubscribe(std::string_view topic) {
    if (!client) {
        ESP_LOGD(TAG, "MQTT client not initialized, cannot unsubscribe from topic: %s", topic.data());
        return;
    }

    if (topic.empty()) {
        ESP_LOGE(TAG, "Empty topic for unsubscription");
        return;
    }

    ESP_LOGD(TAG, "Unsubscribing from topic: %.*s", topic.length(), topic.data());

    // Remove from internal subscription list
    auto it = std::ranges::find_if(subscriptions_,
                                   [topic](const SubscriptionEntry& entry) { return entry.topic == topic; });
    
    if (it != subscriptions_.end()) {
        subscriptions_.erase(it);
    }

    if (enabled) {
        if (int msg_id = esp_mqtt_client_unsubscribe(client, topic.data()); msg_id < 0) {
            ESP_LOGW(TAG, "Failed to unsubscribe from topic: %s, error: %d", topic.data(), msg_id);
        }
    }
}

void MqttBroker::unsubscribe(MqttSubscriber* subscriber) {
    if (!subscriber) {
        ESP_LOGE(TAG, "Cannot unsubscribe null subscriber");
        return;
    }

    // Remove subscriber from all topics and clean up empty topics
    for (auto it = subscriptions_.begin(); it != subscriptions_.end();) {
        // Remove subscriber from this topic
        auto& subscribers = it->subscribers;
        std::erase(subscribers, subscriber);
        
        // If no subscribers left, unsubscribe from broker and remove topic
        if (subscribers.empty()) {
            ESP_LOGD(TAG, "No more subscribers for topic %s, unsubscribing from broker", it->topic.c_str());
            if (enabled && client) {
                esp_mqtt_client_unsubscribe(client, it->topic.c_str());
            }
            it = subscriptions_.erase(it);
        } else {
            ++it;
        }
    }
}

void MqttBroker::mqtt_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data) {
    const auto* broker = static_cast<MqttBroker*>(handler_args);
    auto* event = static_cast<esp_mqtt_event_handle_t>(event_data);
    broker->handleMqttEvent(event);
}

void MqttBroker::handleMqttEvent(esp_mqtt_event_handle_t event) const {
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT %s broker connected", url.c_str());
            const_cast<MqttBroker*>(this)->connected_ = true;
            sendBirth();
            
            // Resubscribe to all existing subscriptions
            const_cast<MqttBroker*>(this)->resubscribeAll();
            
            if (on_connected_callback_) {
                on_connected_callback_(brokerId);
            }
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT broker %s disconnected", url.c_str());
            const_cast<MqttBroker*>(this)->connected_ = false;
            if (on_disconnected_callback_) {
                on_disconnected_callback_(brokerId);
            }
            break;
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "Subscribed to topic (msg_id: %d)", event->msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "Unsubscribed from topic (msg_id: %d)", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "Message published (msg_id: %d)", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            {
                ESP_LOGI(TAG, "Message received on topic: %.*s", event->topic_len, event->topic);
                
                // Dispatch to subscribers
                std::string_view topic_view(event->topic, event->topic_len);
                std::string_view data_view(event->data, event->data_len);
                
                for (const auto& subscription : subscriptions_) {
                    if (subscription.topic == topic_view) {
                        for (auto* subscriber : subscription.subscribers) {
                            subscriber->onMessage(brokerId, topic_view, data_view);
                        }
                        break;
                    }
                }
                
                // Keep existing callback for backward compatibility
                if (on_message_callback_) {
                    on_message_callback_(brokerId, event->topic, event->data, event->data_len);
                }
                break;
            }
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT error occurred");
            if (on_error_callback_ && event->error_handle) {
                on_error_callback_(brokerId, event->error_handle);
            }
            break;
        default:
            ESP_LOGW(TAG, "Unhandled MQTT event: %d", event->event_id);
            break;
    }
}

// Callback setters
void MqttBroker::setOnConnectedCallback(std::function<void(uint8_t)> callback) {
    on_connected_callback_ = callback;
}

void MqttBroker::setOnDisconnectedCallback(std::function<void(uint8_t)> callback) {
    on_disconnected_callback_ = callback;
}

void MqttBroker::setOnMessageCallback(std::function<void(uint8_t, const char*, const char*, size_t)> callback) {
    on_message_callback_ = callback;
}

void MqttBroker::setOnErrorCallback(std::function<void(uint8_t, esp_mqtt_error_codes*)> callback) {
    on_error_callback_ = callback;
}

void MqttBroker::resubscribeAll() {
    if (!enabled || !client) {
        ESP_LOGW(TAG, "Cannot resubscribe: broker not enabled or client not initialized");
        return;
    }

    ESP_LOGI(TAG, "Resubscribing to %d topics", subscriptions_.size());

    // Resubscribe to all topics that have subscribers
    for (const auto& subscription : subscriptions_) {
        if (!subscription.subscribers.empty()) {
            ESP_LOGD(TAG, "Resubscribing to topic: %s", subscription.topic.c_str());
            // Use QoS 1 as default for resubscription - in a more sophisticated implementation 
            // you might want to store the original QoS per subscription
            if (int msg_id = esp_mqtt_client_subscribe(client, subscription.topic.c_str(), 1); msg_id < 0) {
                ESP_LOGW(TAG, "Failed to resubscribe to topic: %s, error: %d", subscription.topic.c_str(), msg_id);
            }
        }
    }
}
