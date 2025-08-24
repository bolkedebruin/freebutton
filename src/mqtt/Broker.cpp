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

    ESP_LOGI(TAG, "topic: %.*s data: %.*s\n", topic.length(), topic.data(), data.length(), data.data());

    if (enabled) {
        // ensure null terminated with no heap allocation
        char topic_buf[topic.length() + 1];
        char data_buf[data.length() + 1];

        std::memcpy(topic_buf, topic.data(), topic.length());
        std::memcpy(data_buf, data.data(), data.length());
        topic_buf[topic.length()] = '\0';
        data_buf[data.length()] = '\0';

        if (int ret = esp_mqtt_client_publish(client, topic_buf, data_buf, 0, qos, retain); ret < 0) {
            ESP_LOGW(TAG, "Failed to enqueue message: %d\n", ret);
        }
    }
}

void MqttBroker::subscribe(std::string_view topic, const int qos) const {
    if (!client) {
        ESP_LOGE(TAG, "MQTT client not initialized, cannot subscribe to topic: %s\n", topic.data());
        return;
    }

    if (topic.empty()) {
        ESP_LOGE(TAG, "Empty topic for subscription");
        return;
    }

    ESP_LOGD(TAG, "Subscribing to topic: %.*s\n", topic.length(), topic.data());

    if (enabled) {
        if (int msg_id = esp_mqtt_client_subscribe(client, topic.data(), qos); msg_id < 0) {
            ESP_LOGW(TAG, "Failed to subscribe to topic: %s, error: %d\n", topic.data(), msg_id);
        }
    }
}

void MqttBroker::unsubscribe(std::string_view topic) const {
    if (!client) {
        ESP_LOGE(TAG, "MQTT client not initialized, cannot unsubscribe from topic: %s\n", topic.data());
        return;
    }

    if (topic.empty()) {
        ESP_LOGE(TAG, "Empty topic for unsubscription");
        return;
    }

    ESP_LOGD(TAG, "Unsubscribing from topic: %.*s\n", topic.length(), topic.data());

    if (enabled) {
        if (int msg_id = esp_mqtt_client_unsubscribe(client, topic.data()); msg_id < 0) {
            ESP_LOGW(TAG, "Failed to unsubscribe from topic: %s, error: %d\n", topic.data(), msg_id);
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
            sendBirth();
            if (on_connected_callback_) {
                on_connected_callback_(brokerId);
            }
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT broker %s disconnected", url.c_str());
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
            ESP_LOGI(TAG, "Message received on topic: %.*s", event->topic_len, event->topic);
            if (on_message_callback_) {
                on_message_callback_(brokerId, event->topic, event->data, event->data_len);
            }
            break;
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
