//
// seam_mqtt.cpp - MQTT for fb-emulator.wasm.
//
// The firmware's script side of MQTT is compiled in unchanged:
// src/scripting/mqtt_binding.cpp (the 2 KB event ring between the MQTT task
// and the loop task, the drop counter, processPending) is included below, and
// src/mqtt/Registry.cpp / Component.cpp are built next to it. What is not
// there is esp-mqtt: the MqttBroker members the binding calls are defined
// here, on top of the host's transport (web/src/js/emulator/mqtt.ts).
//
#include <Arduino.h>
#include <esp_log.h>

#include <string>
#include <string_view>

#include "fbh.h"

// Included, not compiled separately, so that the queue and its drop counter
// (anonymous namespace there) can be reported by fbe_stats().
#include "scripting/mqtt_binding.cpp"

#define FBE_EXPORT(name) __attribute__((export_name(#name), used))

// --- MqttBroker without a client ---------------------------------------------------

void MqttBroker::start() { connected_.store(true); }
void MqttBroker::stop() { connected_.store(false); }

void MqttBroker::updateConfig(const std::string& newUrl, uint32_t, uint32_t, const std::string&, const std::string&,
                              const bool) {
    url = newUrl;
}

void MqttBroker::publish(const std::string_view topic, const std::string_view data, const int qos,
                         const int retain) const {
    fbh_mqtt_publish(brokerId, topic.data(), topic.size(), data.data(), data.size(), qos, retain);
}

// As the firmware: one broker subscription per topic, however many
// components ask for it.
void MqttBroker::subscribe(const std::string_view topic, const int qos, MqttSubscriber* subscriber) {
    for (auto& entry : subscriptions_) {
        if (entry.topic == topic) {
            for (const auto* existing : entry.subscribers) {
                if (existing == subscriber) {
                    return;
                }
            }
            entry.subscribers.push_back(subscriber);
            return;
        }
    }
    subscriptions_.emplace_back(topic, static_cast<uint8_t>(qos), subscriber);
    fbh_mqtt_subscribe(brokerId, topic.data(), topic.size(), qos);
}

void MqttBroker::unsubscribe(const std::string_view topic) {
    for (auto it = subscriptions_.begin(); it != subscriptions_.end(); ++it) {
        if (it->topic == topic) {
            fbh_mqtt_unsubscribe(brokerId, it->topic.data(), it->topic.size());
            subscriptions_.erase(it);
            return;
        }
    }
}

void MqttBroker::unsubscribe(MqttSubscriber* subscriber) {
    for (auto it = subscriptions_.begin(); it != subscriptions_.end();) {
        auto& subscribers = it->subscribers;
        for (auto s = subscribers.begin(); s != subscribers.end();) {
            s = (*s == subscriber) ? subscribers.erase(s) : s + 1;
        }
        if (subscribers.empty()) {
            fbh_mqtt_unsubscribe(brokerId, it->topic.data(), it->topic.size());
            it = subscriptions_.erase(it);
        } else {
            ++it;
        }
    }
}

extern "C" {

// A broker of config.json "brokers", in file order: its index is the broker
// id scripts use. Returns the id, 255 on error.
FBE_EXPORT(fbe_broker_add) int fbe_broker_add(const char* name) {
    return MqttBrokerRegistry::instance().addBroker(std::make_unique<MqttBroker>(name, "emulator", false));
}

FBE_EXPORT(fbe_broker_count) int fbe_broker_count(void) {
    return mqtt_binding_get_broker_count();
}

// The transport connected / lost its connection: the broker's state, and
// the event for mqtt.onConnect / onDisconnect (queued like a message; not
// queued while no script uses MQTT, as on the device).
// quiet != 0: only the state (a script that starts on a connected broker
// gets no onConnect, docs/SCRIPT-RESTART.md).
FBE_EXPORT(fbe_broker_set_connected) void fbe_broker_set_connected(const int id, const int connected, const int quiet) {
    auto* broker = MqttBrokerRegistry::getBroker(static_cast<uint8_t>(id));
    if (!broker) {
        return;
    }
    if (connected) {
        broker->start();
        if (!quiet) MqttBinding::notifyConnected(static_cast<uint8_t>(id));
    } else {
        broker->stop();
        if (!quiet) MqttBinding::notifyDisconnected(static_cast<uint8_t>(id));
    }
}

// A message from the transport (MqttBroker::dispatchMessage ->
// MqttBinding::notifyMessageReceived). Returns 1 = queued for the script,
// 0 = no script subscription wants it, -1 = dropped: the event ring is full,
// -2 = dropped: larger than esp-mqtt's in-buffer (CONFIG_MQTT_BUFFER_SIZE;
// the firmware does not reassemble such a message).
FBE_EXPORT(fbe_mqtt_message)
int fbe_mqtt_message(const int broker, const char* topic, const uint32_t topic_len, const char* payload,
                     const uint32_t payload_len) {
    // PUBLISH packet, QoS 0: fixed header, remaining length, topic length.
    const uint32_t remaining = 2 + topic_len + payload_len;
    const uint32_t packet = 1 + (remaining < 128 ? 1 : remaining < 16384 ? 2 : 3) + remaining;
    if (packet > FB_MQTT_BUFFER_SIZE) {
        ESP_LOGW("MqttBroker", "Dropping %u B message on %.*s: larger than the MQTT in-buffer",
                 static_cast<unsigned>(payload_len), static_cast<int>(topic_len), topic);
        return -2;
    }
    const mqtt_js_message_filter_t filter = g_js_message_filter.load();
    if (!filter || !filter(static_cast<uint8_t>(broker), topic, topic_len)) {
        return 0;
    }
    const uint32_t dropped = g_dropped.load();
    MqttBinding::notifyMessageReceived(static_cast<uint8_t>(broker), topic, topic_len, payload, payload_len);
    return g_dropped.load() == dropped ? 1 : -1;
}

// MqttBinding::processPending() of one loop() pass; nonzero when events are
// left for the next pass.
int fbe_mqtt_process_pending(void) {
    MqttBinding::processPending();
    return g_queue.empty() ? 0 : 1;
}

uint32_t fbe_mqtt_dropped(void) { return g_dropped.load(); }
uint32_t fbe_mqtt_queue_used(void) { return static_cast<uint32_t>(g_queue.used()); }
uint32_t fbe_mqtt_queue_bytes(void) { return static_cast<uint32_t>(kQueueBytes); }

} // extern "C"
