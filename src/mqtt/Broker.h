//
// Created by Bolke de Bruin on 10/08/2025.
//

#ifndef BROKER_H
#define BROKER_H
#include <atomic>
#include <functional>
#include <mqtt_client.h>
#include <string>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "Component.h"
#include "config.h"
#include "json/JsonWriter.h"

class MqttBroker {
public:
    MqttBroker(const std::string& name, const std::string& url, const bool autoDiscovery) : brokerId(0), name(std::move(name)),
                                                                  url(std::move(url)), autoDiscovery_(autoDiscovery) {
        port = 0;
        wsPort = 0;
        client = nullptr;
        username = "";
        password = "";

        deviceTopic = BASE_TOPIC + deviceId;
        statusTopic_ = deviceTopic + "/status";
        subscriptionsLock_ = xSemaphoreCreateRecursiveMutexStatic(&subscriptionsLockBuf_);
    }

    // Constructor for full broker configuration from JSON
    MqttBroker(const std::string& name, const std::string& url, uint32_t port, uint32_t wsPort,
               const std::string& username, const std::string& password, const bool autoDiscovery)
        : brokerId(0), name(name), url(url), port(port), wsPort(wsPort),
          username(username), password(password), autoDiscovery_(autoDiscovery), client(nullptr) {
        deviceTopic = BASE_TOPIC + deviceId;
        statusTopic_ = deviceTopic + "/status";
        subscriptionsLock_ = xSemaphoreCreateRecursiveMutexStatic(&subscriptionsLockBuf_);
    }

    // Holds a static mutex and is registered with esp-mqtt by brokerId: not copyable or movable.
    MqttBroker(const MqttBroker&) = delete;
    MqttBroker& operator=(const MqttBroker&) = delete;

    void start();
    void stop();

    void sendBirth() const;

    void publish(std::string_view topic, std::string_view data, int qos, int retain) const;
    // Zero-copy: `topic` and `payload` are already NUL-terminated and handed
    // to esp-mqtt as they are (length 0 = empty payload, e.g. to clear a
    // retained config). Returns esp_mqtt_client_publish's result: the message
    // id, or negative when the message could not be enqueued.
    int publish(const char* topic, const char* payload, size_t length, int qos, int retain) const;
    void subscribe(std::string_view topic, int qos, MqttSubscriber* subscriber);
    void unsubscribe(std::string_view topic);
    void unsubscribe(MqttSubscriber* subscriber);

    // Connection state
    [[nodiscard]] bool isConnected() const noexcept { return connected_.load(); }

    // Bytes waiting in the esp-mqtt outbox (QoS>0 messages not yet acknowledged).
    [[nodiscard]] int outboxSize() const { return client ? esp_mqtt_client_get_outbox_size(client) : 0; }

    // Event callbacks
    void setOnConnectedCallback(std::function<void(uint8_t)> callback);
    void setOnDisconnectedCallback(std::function<void(uint8_t)> callback);
    void setOnMessageCallback(std::function<void(uint8_t, const char*, const char*, size_t)> callback);
    void setOnErrorCallback(std::function<void(uint8_t, esp_mqtt_error_codes*)> callback);

    // Instance event handler
    void handleMqttEvent(esp_mqtt_event_handle_t event) const;
    
    // Resubscribe to all existing subscriptions (called on reconnect). Sends the
    // first SUBSCRIBE batch only; each SUBACK (MQTT_EVENT_SUBSCRIBED) sends the
    // next one, so the MQTT task keeps draining the socket in between.
    void resubscribeAll() const;

    void setBrokerId(const uint8_t id) { brokerId = id; }
    uint8_t getBrokerId() const { return brokerId; }

    std::string_view getName() const { return name; }

    // Update broker configuration (for auto-registered placeholders)
    void updateConfig(const std::string& newUrl, uint32_t newPort = 0, uint32_t newWsPort = 0,
                     const std::string& newUsername = "", const std::string& newPassword = "",
                     const bool newAutoDiscovery = true);

    // Check if broker is configured (has valid URL)
    bool isConfigured() const { return !url.empty(); }
    bool hasAutoDiscovery() const { return autoDiscovery_; }

    // Serialize broker configuration to JSON
    template<typename OutputStream>
    void toJson(json::JsonWriter<OutputStream>::ScopedObject &obj) const {
        obj.Add("brokerid", name);
        obj.Add("url", url);
        obj.Add("port", static_cast<int>(port));
        obj.Add("wsport", static_cast<int>(wsPort));
        obj.Add("username", username);
        obj.Add("password", password);
        obj.Add("hassdiscovery", autoDiscovery_);
    }

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
    std::string statusTopic_;  // <deviceTopic>/status: LWT and birth, built once

    bool enabled = false;
    std::atomic<bool> connected_{false};
    bool autoDiscovery_ = false;

    // Event callbacks
    std::function<void(uint8_t)> on_connected_callback_;
    std::function<void(uint8_t)> on_disconnected_callback_;
    std::function<void(uint8_t, const char*, const char*, size_t)> on_message_callback_;
    std::function<void(uint8_t, esp_mqtt_error_codes*)> on_error_callback_;

    // Static event handler
    static void mqtt_event_handler(void* handler_args, esp_event_base_t base,
                                  int32_t event_id, void* event_data);

    struct SubscriptionEntry {
        SubscriptionEntry(const std::string_view topic, const uint8_t qos, MqttSubscriber* subscriber)
            : topic(topic), subscribers{subscriber}, qos(qos) {}

        std::string topic;
        std::vector<MqttSubscriber*> subscribers;  // Multiple components can subscribe to same topic
        uint8_t qos = 0;     // Requested QoS, used for every (re)subscribe
        bool sent = false;   // SUBSCRIBE sent on the current connection
    };

    // Guarded by subscriptionsLock_ (recursive, statically allocated). Any
    // task may append/remove entries; only the MQTT task talks to esp-mqtt
    // about them (SUBSCRIBE/UNSUBSCRIBE), from pumpSubscriptions().
    // An entry with no subscribers is a pending UNSUBSCRIBE.
    std::vector<SubscriptionEntry> subscriptions_;
    StaticSemaphore_t subscriptionsLockBuf_{};
    SemaphoreHandle_t subscriptionsLock_ = nullptr;

    // Subscription pump state, guarded by subscriptionsLock_.
    bool subscriptionWork_ = false;      // unsent entries or pending unsubscribes may exist
    int batchMsgId_ = -1;                // msg_id of the SUBSCRIBE awaiting its SUBACK
    uint32_t batchSentTick_ = 0;         // FreeRTOS tick that SUBSCRIBE was sent
    uint32_t lastBatchWarnTick_ = 0;     // rate limit for SUBSCRIBE send-failure warnings
    uint32_t lastUnsubscribeWarnTick_ = 0;  // rate limit for UNSUBSCRIBE send-failure warnings

    // Waking the MQTT task from other tasks (custom MQTT_USER_EVENT).
    std::atomic<bool> wakePosted_{false};
    std::atomic<TaskHandle_t> mqttTask_{nullptr};

    void startResubscribe();
    void stopResubscribe();
    void pumpSubscriptions();           // MQTT task only
    void requestSubscriptionWork();     // any task; call without holding the lock
    void dispatchMessage(esp_mqtt_event_handle_t event) const;
};

#endif //BROKER_H
