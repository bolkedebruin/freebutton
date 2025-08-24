//
// Created by Bolke de Bruin on 10/08/2025.
//

#ifndef BROKER_H
#define BROKER_H
#include <functional>
#include <mqtt_client.h>
#include <string>

#include "config.h"

class MqttBroker {
public:
    MqttBroker(const uint8_t brokerId, const std::string& url) : brokerId(brokerId), url(std::move(url)) {
        port = 0;
        wsPort = 0;
        client = nullptr;
        username = "";
        password = "";

        deviceTopic = BASE_TOPIC + deviceId;
    }

    void start();
    //void stop();

    void sendBirth() const;

    void publish(std::string_view topic, std::string_view data, int qos, int retain) const;
    void subscribe(std::string_view topic, int qos) const;
    void unsubscribe(std::string_view topic) const;

    // Event callbacks
    void setOnConnectedCallback(std::function<void(uint8_t)> callback);
    void setOnDisconnectedCallback(std::function<void(uint8_t)> callback);
    void setOnMessageCallback(std::function<void(uint8_t, const char*, const char*, size_t)> callback);
    void setOnErrorCallback(std::function<void(uint8_t, esp_mqtt_error_codes*)> callback);

    // Instance event handler
    void handleMqttEvent(esp_mqtt_event_handle_t event) const;

private:
    uint8_t brokerId;
    std::string url;

    uint32_t port = 0;
    uint32_t wsPort = 0;
    std::string username;
    std::string password;

    esp_mqtt_client_handle_t client = nullptr;

    std::string deviceTopic;

    bool enabled = false;

    // Event callbacks
    std::function<void(uint8_t)> on_connected_callback_;
    std::function<void(uint8_t)> on_disconnected_callback_;
    std::function<void(uint8_t, const char*, const char*, size_t)> on_message_callback_;
    std::function<void(uint8_t, esp_mqtt_error_codes*)> on_error_callback_;

    // Static event handler
    static void mqtt_event_handler(void* handler_args, esp_event_base_t base,
                                  int32_t event_id, void* event_data);
};

#endif //BROKER_H
