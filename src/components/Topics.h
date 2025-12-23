//
// Created by Bolke de Bruin on 19/11/2025.
//

#ifndef FREEBUTTON_TOPICS_H
#define FREEBUTTON_TOPICS_H
#include <cstdint>
#include <string>

#include "types.h"
#include "ArduinoJson/Object/JsonObject.hpp"
#include "mqtt/Registry.h"
#include "json/JsonWriter.h"

struct Topic {
    uint8_t brokerId;
    std::string topic;

    std::string payload;

    EventType eventType;

    void fromJson(const ArduinoJson::JsonObject &data) {
        brokerId = data["brokerid"].isNull() ? brokerId : MqttBrokerRegistry::getOrCreateBrokerId(data["brokerid"].as<std::string>());
        topic = data["topic"].isNull() ? topic : data["topic"].as<std::string>();
        payload = data["payload"].isNull() ? payload : data["payload"].as<std::string>();

        eventType = static_cast<EventType>(data["eventtype"].as<int>());
    }

    template<typename OutputStream>
    void toJson(json::JsonWriter<OutputStream>::ScopedObject &obj) const {
        // Get broker name from ID
        auto* broker = MqttBrokerRegistry::getBroker(brokerId);
        if (broker) {
            obj.Add("brokerid", std::string(broker->getName()));
        }
        obj.Add("topic", topic);
        obj.Add("payload", payload);
        obj.Add("eventtype", static_cast<int>(eventType));
    }
};

#endif //FREEBUTTON_TOPICS_H