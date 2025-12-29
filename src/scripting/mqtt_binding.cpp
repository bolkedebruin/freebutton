/*
 * MQTT JavaScript Binding Abstraction Layer Implementation
 *
 * Bridges JavaScript MQTT bindings to C++ MQTT broker implementation
 */

#include "mqtt_binding.h"
#include "../mqtt/Registry.h"
#include "../mqtt/Broker.h"
#include "esp_log.h"
#include <cstring>

static const char* TAG = "mqtt_hw";

// Global callbacks for JavaScript
static mqtt_js_message_callback_t g_js_message_callback = nullptr;
static mqtt_js_connect_callback_t g_js_connect_callback = nullptr;
static mqtt_js_disconnect_callback_t g_js_disconnect_callback = nullptr;

// C-compatible wrapper functions
extern "C" {

int mqtt_binding_publish(uint8_t brokerId, const char* topic, const char* payload, int qos, int retain) {
    if (!topic || !payload) {
        ESP_LOGE(TAG, "mqtt_binding_publish: null topic or payload");
        return -1;
    }

    auto* broker = MqttBrokerRegistry::getBroker(brokerId);
    if (!broker) {
        ESP_LOGE(TAG, "mqtt_binding_publish: invalid brokerId %d", brokerId);
        return -1;
    }

    if (!broker->isConnected()) {
        ESP_LOGW(TAG, "mqtt_binding_publish: broker %d not connected", brokerId);
        return -1;
    }

    ESP_LOGD(TAG, "Publishing to broker %d: %s = %s", brokerId, topic, payload);
    broker->publish(topic, payload, qos, retain);
    return 0;
}

// Dummy subscriber for JavaScript subscriptions
// This allows JavaScript to subscribe to topics without creating a full MqttSubscriber class
class MqttJsSubscriber : public MqttSubscriber {
public:
    void onMessage(uint8_t, std::string_view, std::string_view) override {
        // Messages are handled by the JavaScript callback layer
        // This is just a placeholder to register the subscription
    }

    const std::string getComponentType() const override {
        return "javascript_mqtt_subscriber";
    }
};

// Single global subscriber instance for all JavaScript subscriptions
static MqttJsSubscriber g_js_subscriber;

int mqtt_binding_subscribe(uint8_t brokerId, const char* topic, int qos) {
    if (!topic) {
        ESP_LOGE(TAG, "mqtt_binding_subscribe: null topic");
        return -1;
    }

    auto* broker = MqttBrokerRegistry::getBroker(brokerId);
    if (!broker) {
        ESP_LOGE(TAG, "mqtt_binding_subscribe: invalid brokerId %d", brokerId);
        return -1;
    }

    // Subscribe through broker using the global JS subscriber
    // This ensures the MQTT client actually subscribes to the topic
    ESP_LOGD(TAG, "Subscribing to broker %d topic: %s", brokerId, topic);
    broker->subscribe(topic, qos, &g_js_subscriber);

    return 0;
}

int mqtt_binding_unsubscribe(uint8_t brokerId, const char* topic) {
    if (!topic) {
        ESP_LOGE(TAG, "mqtt_binding_unsubscribe: null topic");
        return -1;
    }

    auto* broker = MqttBrokerRegistry::getBroker(brokerId);
    if (!broker) {
        ESP_LOGE(TAG, "mqtt_binding_unsubscribe: invalid brokerId %d", brokerId);
        return -1;
    }

    ESP_LOGD(TAG, "Unsubscribing from broker %d topic: %s", brokerId, topic);
    broker->unsubscribe(topic);
    return 0;
}

int mqtt_binding_is_connected(uint8_t brokerId) {
    auto* broker = MqttBrokerRegistry::getBroker(brokerId);
    if (!broker) {
        return 0;
    }
    return broker->isConnected() ? 1 : 0;
}

const char* mqtt_binding_get_broker_name(uint8_t brokerId) {
    auto* broker = MqttBrokerRegistry::getBroker(brokerId);
    if (!broker) {
        return nullptr;
    }
    return broker->getName().data();
}

int mqtt_binding_get_broker_count(void) {
    int count = 0;
    MqttBrokerRegistry::forEachBroker([&count](uint8_t, MqttBroker*) {
        count++;
    });
    return count;
}

void mqtt_binding_register_message_callback(mqtt_js_message_callback_t callback) {
    g_js_message_callback = callback;
    ESP_LOGI(TAG, "Registered JavaScript message callback: %p", (void*)callback);
}

void mqtt_binding_register_connect_callback(mqtt_js_connect_callback_t callback) {
    g_js_connect_callback = callback;
    ESP_LOGI(TAG, "Registered JavaScript connect callback: %p", (void*)callback);
}

void mqtt_binding_register_disconnect_callback(mqtt_js_disconnect_callback_t callback) {
    g_js_disconnect_callback = callback;
    ESP_LOGI(TAG, "Registered JavaScript disconnect callback: %p", (void*)callback);
}

} // extern "C"

/*
 * C++ functions to invoke JavaScript callbacks from MQTT broker
 * These are called from the MQTT broker event handlers
 */

namespace MqttBinding {

void notifyMessageReceived(uint8_t brokerId, const char* topic, const char* payload, size_t length) {
    if (g_js_message_callback) {
        ESP_LOGD(TAG, "Notifying JS: message on broker %d topic %s", brokerId, topic);
        g_js_message_callback(brokerId, topic, payload, length);
    }
}

void notifyConnected(uint8_t brokerId) {
    if (g_js_connect_callback) {
        ESP_LOGI(TAG, "Notifying JS: broker %d connected", brokerId);
        g_js_connect_callback(brokerId);
    }
}

void notifyDisconnected(uint8_t brokerId) {
    if (g_js_disconnect_callback) {
        ESP_LOGI(TAG, "Notifying JS: broker %d disconnected", brokerId);
        g_js_disconnect_callback(brokerId);
    }
}

} // namespace MqttBinding
