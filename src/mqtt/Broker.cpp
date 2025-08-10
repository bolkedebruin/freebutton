//
// Created by Bolke de Bruin on 10/08/2025.
//
#include "Broker.h"

#include <algorithm>

#include "esp_log.h"
#include "Handler.h"

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

void MqttBroker::sendBirth() const {
    const std::string lwt_topic = std::string(deviceTopic) + "/status";
    publish(lwt_topic, "online", 1, 1);
}

void MqttBroker::publish(std::string_view topic, std::string_view data, const int qos, const int retain) const {
    if (topic.length() == 0) {
        ESP_LOGE(TAG, "Empty topic (data: %.*s)", data.length(), data.data());
        return;
    }

    ESP_LOGD(TAG, "topic: %.*s data: %.*s\n", topic.length(), topic.data(), data.length(), data.data());

    if (enabled) {
        if (int ret = esp_mqtt_client_publish(client, topic.data(), data.data(), data.length(), qos, retain); ret < 0) {
            ESP_LOGW(TAG, "Failed to enqueue message: %d\n", ret);
        }
    }
}