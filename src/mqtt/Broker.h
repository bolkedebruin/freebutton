//
// Created by Bolke de Bruin on 10/08/2025.
//

#ifndef BROKER_H
#define BROKER_H
#include <functional>
#include <mqtt_client.h>
#include <string>

#include "Component.h"
#include "config.h"

class MqttBroker {
public:
    MqttBroker(const std::string& name, const std::string& url) : brokerId(0), name(std::move(name)),
                                                                  url(std::move(url)) {
        port = 0;
        wsPort = 0;
        client = nullptr;
        username = "";
        password = "";

        deviceTopic = BASE_TOPIC + deviceId;
    }

    void start();
    void stop();

    void sendBirth() const;

    void publish(std::string_view topic, std::string_view data, int qos, int retain) const;
    void subscribe(std::string_view topic, int qos, MqttSubscriber* subscriber);
    void unsubscribe(std::string_view topic);
    void unsubscribe(MqttSubscriber* subscriber);

    // Event callbacks
    void setOnConnectedCallback(std::function<void(uint8_t)> callback);
    void setOnDisconnectedCallback(std::function<void(uint8_t)> callback);
    void setOnMessageCallback(std::function<void(uint8_t, const char*, const char*, size_t)> callback);
    void setOnErrorCallback(std::function<void(uint8_t, esp_mqtt_error_codes*)> callback);

    // Instance event handler
    void handleMqttEvent(esp_mqtt_event_handle_t event) const;

    void setBrokerId(const uint8_t id) { brokerId = id; }
    uint8_t getBrokerId() const { return brokerId; }

    std::string_view getName() const { return name; }

private:
    uint8_t brokerId;

    std::string name;
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

    struct SubscriptionEntry {
        std::string topic;
        std::vector<MqttSubscriber*> subscribers;  // Multiple components can subscribe to same topic
    };

    std::vector<SubscriptionEntry> subscriptions_;
};

#endif //BROKER_H
