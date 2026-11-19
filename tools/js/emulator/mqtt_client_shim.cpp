//
// mqtt_client_shim.cpp - esp-mqtt for fb-emulator.wasm (shim/mqtt_client.h).
//
// src/mqtt/Broker.cpp is the firmware's and talks to this client as it does
// to esp-mqtt: it starts it with a last will, gets CONNECTED / DISCONNECTED /
// SUBSCRIBED / DATA events on "the MQTT task", publishes, subscribes in
// batches and waits for each SUBACK. The transport behind the client is the
// host's (web/src/js/emulator/mqtt.ts); events are queued and delivered by
// fbe_mqtt_pump(), never from inside another event.
//
#include <mqtt_client.h>

#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "emu.h"
#include "fbh.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" {
#include "scripting/js/mqtt_topic_match.h"
}

struct esp_mqtt_client {
    int broker = -1;
    esp_event_handler_t handler = nullptr;
    void* handlerArgs = nullptr;
    bool started = false;
    bool connected = false;
    std::string uri, willTopic, willPayload;
    int willQos = 0, willRetain = 0;
    int nextMsgId = 1;
    std::vector<std::string> filters;
};

namespace {

constexpr int kMaxBrokers = 8;
esp_mqtt_client* g_clients[kMaxBrokers] = {};
// What the host last said about each transport, also before the client starts.
bool g_transportUp[kMaxBrokers] = {};

struct Pending {
    esp_mqtt_client* client;
    esp_mqtt_event_id_t id;
    int msgId;
    std::string topic;
    std::string data;
    int totalLen;  // of the message; larger than data for a fragment
};
std::deque<Pending> g_queue;
bool g_dispatching = false;

int g_loopTask;
int g_mqttTask;
TaskHandle_t g_currentTask = &g_loopTask;

int g_origin = FBH_ORIGIN_FIRMWARE;
int g_kind = FBH_PUBLISH_STATE;

bool alive(const esp_mqtt_client* client) {
    for (const auto* c : g_clients) {
        if (c == client) return true;
    }
    return false;
}

void dispatch(Pending& p) {
    if (!alive(p.client) || !p.client->handler) {
        return;
    }
    esp_mqtt_event_t event = {};
    event.event_id = p.id;
    event.client = p.client;
    event.msg_id = p.msgId;
    event.topic = p.topic.data();
    event.topic_len = static_cast<int>(p.topic.size());
    event.data = p.data.data();
    event.data_len = static_cast<int>(p.data.size());
    event.total_data_len = p.totalLen;
    const TaskHandle_t before = g_currentTask;
    g_currentTask = &g_mqttTask;
    // The birth message is the only publish of a CONNECTED event.
    const int kind = p.id == MQTT_EVENT_CONNECTED ? fbe_publish_scope(FBH_ORIGIN_FIRMWARE, FBH_PUBLISH_AVAILABILITY) : -1;
    p.client->handler(p.client->handlerArgs, "MQTT_EVENTS", p.id, &event);
    if (kind >= 0) fbe_publish_scope_end(kind);
    g_currentTask = before;
}

void post(esp_mqtt_client* client, const esp_mqtt_event_id_t id, const int msgId = 0) {
    g_queue.push_back({client, id, msgId, {}, {}, 0});
}

} // namespace

extern "C" {

TaskHandle_t xTaskGetCurrentTaskHandle(void) { return g_currentTask; }

int fbe_publish_scope(const int origin, const int kind) {
    const int previous = (g_origin << 8) | g_kind;
    g_origin = origin;
    g_kind = kind;
    return previous;
}

void fbe_publish_scope_end(const int previous) {
    g_origin = previous >> 8;
    g_kind = previous & 0xff;
}

void fbe_mqtt_pump(void) {
    if (g_dispatching) {
        return;
    }
    g_dispatching = true;
    while (!g_queue.empty()) {
        Pending p = std::move(g_queue.front());
        g_queue.pop_front();
        dispatch(p);
    }
    g_dispatching = false;
}

esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t* config) {
    auto* client = new (std::nothrow) esp_mqtt_client();
    if (!client) return nullptr;
    client->uri = config->broker.address.uri ? config->broker.address.uri : "";
    client->willTopic = config->session.last_will.topic ? config->session.last_will.topic : "";
    client->willPayload = config->session.last_will.msg ? config->session.last_will.msg : "";
    client->willQos = config->session.last_will.qos;
    client->willRetain = config->session.last_will.retain;
    return client;
}

// The firmware registers with its broker id as the handler argument.
esp_err_t esp_mqtt_client_register_event(esp_mqtt_client_handle_t client, esp_mqtt_event_id_t event,
                                         esp_event_handler_t handler, void* handler_args) {
    (void)event;
    client->handler = handler;
    client->handlerArgs = handler_args;
    if (handler) {
        const int broker = static_cast<int>(reinterpret_cast<uintptr_t>(handler_args));
        if (broker >= 0 && broker < kMaxBrokers) {
            client->broker = broker;
            g_clients[broker] = client;
        }
    }
    return ESP_OK;
}

esp_err_t esp_mqtt_client_start(esp_mqtt_client_handle_t client) {
    if (client->broker < 0) return ESP_FAIL;
    client->started = true;
    fbh_mqtt_start(client->broker, client->uri.data(), client->uri.size(), client->willTopic.data(),
                   client->willTopic.size(), client->willPayload.data(), client->willPayload.size(), client->willQos,
                   client->willRetain);
    if (g_transportUp[client->broker] && !client->connected) {
        client->connected = true;
        post(client, MQTT_EVENT_BEFORE_CONNECT);
        post(client, MQTT_EVENT_CONNECTED);
    }
    return ESP_OK;
}

esp_err_t esp_mqtt_client_stop(esp_mqtt_client_handle_t client) {
    if (client->started && client->broker >= 0) {
        fbh_mqtt_stop(client->broker);
    }
    client->started = false;
    client->connected = false;
    return ESP_OK;
}

esp_err_t esp_mqtt_client_destroy(esp_mqtt_client_handle_t client) {
    for (auto& c : g_clients) {
        if (c == client) c = nullptr;
    }
    delete client;
    return ESP_OK;
}

// As esp-mqtt: the message id (0 for QoS 0), or -1. Not connected: QoS 0 is
// lost, QoS 1 would wait in the outbox; here both are dropped without an
// error (the firmware only publishes QoS 1 while connected).
int esp_mqtt_client_publish(esp_mqtt_client_handle_t client, const char* topic, const char* data, int len,
                            const int qos, const int retain) {
    if (!client || !topic) return -1;
    if (!client->connected) return 0;
    if (len <= 0 && data) len = static_cast<int>(std::strlen(data));
    if (fbh_mqtt_publish(client->broker, topic, std::strlen(topic), data ? data : "", static_cast<size_t>(len), qos,
                         retain, g_origin, g_kind) != 0) {
        return -1;
    }
    return qos > 0 ? client->nextMsgId++ : 0;
}

int esp_mqtt_client_subscribe_multiple(esp_mqtt_client_handle_t client, const esp_mqtt_topic_t* topics,
                                       const int count) {
    if (!client || !client->connected) return -1;
    for (int i = 0; i < count; ++i) {
        const std::string filter = topics[i].filter;
        bool known = false;
        for (const auto& f : client->filters) known = known || f == filter;
        if (!known) client->filters.push_back(filter);
        fbh_mqtt_subscribe(client->broker, filter.data(), filter.size(), topics[i].qos);
    }
    const int msgId = client->nextMsgId++;
    post(client, MQTT_EVENT_SUBSCRIBED, msgId);
    return msgId;
}

int esp_mqtt_client_unsubscribe(esp_mqtt_client_handle_t client, const char* topic) {
    if (!client || !client->connected) return -1;
    for (auto it = client->filters.begin(); it != client->filters.end(); ++it) {
        if (*it == topic) {
            client->filters.erase(it);
            break;
        }
    }
    fbh_mqtt_unsubscribe(client->broker, topic, std::strlen(topic));
    const int msgId = client->nextMsgId++;
    post(client, MQTT_EVENT_UNSUBSCRIBED, msgId);
    return msgId;
}

int esp_mqtt_client_get_outbox_size(esp_mqtt_client_handle_t client) {
    (void)client;
    return 0;
}

esp_err_t esp_mqtt_dispatch_custom_event(esp_mqtt_client_handle_t client, esp_mqtt_event_t* event) {
    post(client, event->event_id);
    return ESP_OK;
}

// The host's transport of broker `broker` came up or went away.
FBE_EXPORT(fbe_mqtt_connected) void fbe_mqtt_connected(const int broker, const int connected) {
    if (broker < 0 || broker >= kMaxBrokers) return;
    g_transportUp[broker] = connected != 0;
    esp_mqtt_client* client = g_clients[broker];
    if (!client || !client->started || client->connected == (connected != 0)) return;
    client->connected = connected != 0;
    if (connected) {
        post(client, MQTT_EVENT_BEFORE_CONNECT);
        post(client, MQTT_EVENT_CONNECTED);
    } else {
        // A new session starts without subscriptions: the firmware makes them again.
        client->filters.clear();
        post(client, MQTT_EVENT_DISCONNECTED);
    }
    // The MQTT task handles it at once, whatever the loop task does.
    fbe_mqtt_pump();
}

// A message from the transport. Returns 1 = a script subscription gets it,
// 2 = only the firmware (a config topic, a Home Assistant command) takes it,
// 0 = no subscription of this client matches (a broker would not send it),
// -1 = the script's event ring is full: dropped, -2 = larger than esp-mqtt's
// in-buffer (CONFIG_MQTT_BUFFER_SIZE): the firmware drops it, -3 = no client.
FBE_EXPORT(fbe_mqtt_message)
int fbe_mqtt_message(const int broker, const char* topic, const uint32_t topic_len, const char* payload,
                     const uint32_t payload_len) {
    if (broker < 0 || broker >= kMaxBrokers || !g_clients[broker] || !g_clients[broker]->connected) return -3;
    esp_mqtt_client* client = g_clients[broker];
    bool subscribed = false;
    for (const auto& filter : client->filters) {
        subscribed = subscribed || mqtt_topic_matches(filter.c_str(), topic, topic_len);
    }
    if (!subscribed) return 0;

    Pending p{client, MQTT_EVENT_DATA, 0, std::string(topic, topic_len), std::string(payload, payload_len),
              static_cast<int>(payload_len)};
    // A PUBLISH packet (QoS 0: fixed header, remaining length, topic length)
    // that does not fit the in-buffer arrives in pieces; the first one is
    // enough for the firmware to see that, and to drop the message.
    const uint32_t remaining = 2 + topic_len + payload_len;
    const uint32_t packet = 1 + (remaining < 128 ? 1 : remaining < 16384 ? 2 : 3) + remaining;
    int result;
    if (packet > FB_MQTT_BUFFER_SIZE) {
        const uint32_t header = packet - payload_len;
        p.data.resize(FB_MQTT_BUFFER_SIZE > header ? FB_MQTT_BUFFER_SIZE - header : 0);
        result = -2;
    } else {
        result = fbe_mqtt_script_wants(broker, topic, topic_len) ? 1 : 2;
    }
    if (g_dispatching) {
        g_queue.push_back(std::move(p));
        return result;
    }
    const uint32_t dropped = fbe_mqtt_dropped();
    g_dispatching = true;
    dispatch(p);
    g_dispatching = false;
    fbe_mqtt_pump();
    return result == 1 && fbe_mqtt_dropped() != dropped ? -1 : result;
}

} // extern "C"
