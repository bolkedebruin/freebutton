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
    
    // Virtual methods for HA discovery - return empty if not applicable
    virtual std::vector<std::string> getSubscriptionTopics(uint8_t brokerId) const { return {}; }
    virtual std::vector<std::string> getPublicationTopics(uint8_t brokerId) const { return {}; }

protected:
    std::string device_id_;
};

class MqttSubscriber : public virtual MqttComponent {
public:
    virtual void onMessage(uint8_t brokerId, std::string_view topic, std::string_view data) = 0;

    void addSubscription(uint8_t brokerId, const std::string &topic, int qos = 0);
    void removeSubscription(uint8_t brokerId, const std::string &topic);
    
    // Override base class method for HA discovery
    std::vector<std::string> getSubscriptionTopics(uint8_t brokerId) const override;
    const std::vector<TopicSubscription>& getSubscriptions() const { return subscriptions; }

private:
    std::vector<TopicSubscription> subscriptions;
};

struct TopicPublication {
    uint8_t brokerId;
    std::string topic;
    int qos = 0;
    bool retain = false;

    TopicPublication(uint8_t brokerId, const std::string &topic, int qos = 0, bool retain = false) :
        brokerId(brokerId), topic(topic), qos(qos), retain(retain) {}
};

class MqttPublisher : public virtual MqttComponent {
public:
    void addPublication(uint8_t brokerId, const std::string &topic, int qos = 0, bool retain = false);
    void removePublication(uint8_t brokerId, const std::string &topic);
    
    // Override base class method for HA discovery
    std::vector<std::string> getPublicationTopics(uint8_t brokerId) const override;
    const std::vector<TopicPublication>& getPublications() const { return publications; }

protected:
    void publish(const std::string &data, int qos = -1, bool retain_override = false);
    static void publish(uint8_t brokerId, const std::string &topic, const std::string &data, int qos = 0, bool retain = false);

private:
    std::vector<TopicPublication> publications;
};

#endif //FREEBUTTON_COMPONENT_H