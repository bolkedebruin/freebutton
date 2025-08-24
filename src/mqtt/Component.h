//
// Created by Bolke de Bruin on 24/08/2025.
//

#ifndef FREEBUTTON_COMPONENT_H
#define FREEBUTTON_COMPONENT_H
#include <string>
#include <vector>

struct TopicSubscription {
    uint8_t brokerId;
    std::string topic;
    int qos = 0;

    TopicSubscription(uint8_t brokerId, const std::string &topic, int qos = 0) :
        brokerId(brokerId), topic(topic), qos(qos) {}
};

// Base interface for all MQTT-enabled components
class MqttComponent {
public:
    virtual ~MqttComponent() = default;
    virtual const std::string getComponentType() const = 0;

protected:
    std::string device_id_;
};

class MqttSubscriber : public virtual MqttComponent {
public:
    virtual void onMessage(uint8_t brokerId, std::string_view topic, std::string_view data) = 0;

    void addSubscription(uint8_t brokerId, const std::string &topic, int qos = 0);
    void removeSubscription(uint8_t brokerId, const std::string &topic);

private:
    std::vector<TopicSubscription> subscriptions;
};

class MqttPublisher : public virtual MqttComponent {
protected:
    void publish(uint8_t brokerId, const std::string &topic, const std::string &data, int qos = 0, bool retain = false);
};

#endif //FREEBUTTON_COMPONENT_H