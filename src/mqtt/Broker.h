//
// Created by Bolke de Bruin on 10/08/2025.
//

#ifndef BROKER_H
#define BROKER_H
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

    //void sendDiscovery() const;

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
};

#endif //BROKER_H
