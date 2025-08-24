//
// Created by Bolke de Bruin on 24/08/2025.
//

#ifndef FREEBUTTON_REGISTRY_H
#define FREEBUTTON_REGISTRY_H
#include <memory>
#include <vector>

#include "Broker.h"

class MqttBrokerRegistry {
private:
    MqttBrokerRegistry() = default;
    ~MqttBrokerRegistry();

    // non copyable, non movable
    MqttBrokerRegistry(const MqttBrokerRegistry&) = delete;
    MqttBrokerRegistry& operator=(const MqttBrokerRegistry&) = delete;

    static inline std::vector<std::unique_ptr<MqttBroker>> brokers;

    uint8_t next_id = 0;

    static constexpr bool namesMatch(const std::string_view name1, const std::string_view name2) {
        return name1 == name2;
    }
public:
    static MqttBrokerRegistry &instance();

    uint8_t addBroker(std::unique_ptr<MqttBroker> broker);

    static uint8_t getBrokerId(std::string_view name);
    static MqttBroker* getBroker(std::string_view name);
    static MqttBroker* getBroker(uint8_t brokerId);

    void connectAll();
    void disconnectAll();


};
#endif //FREEBUTTON_REGISTRY_H