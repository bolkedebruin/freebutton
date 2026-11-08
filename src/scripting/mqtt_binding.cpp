/*
 * MQTT JavaScript Binding Abstraction Layer Implementation
 *
 * Bridges JavaScript MQTT bindings to C++ MQTT broker implementation
 */

#include "mqtt_binding.h"
#include "ScriptEngine.h"
#include "../mqtt/Registry.h"
#include "../mqtt/Broker.h"
#include "../utils/MessageRing.h"
#include "esp_log.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <atomic>
#include <cstring>

static const char* TAG = "mqtt_hw";

// Global callbacks for JavaScript. Written by script code and called by
// processPending(), both with the JS engine lock held.
static mqtt_js_message_callback_t g_js_message_callback = nullptr;
static mqtt_js_connect_callback_t g_js_connect_callback = nullptr;
static mqtt_js_disconnect_callback_t g_js_disconnect_callback = nullptr;
// Read by the MQTT task: nothing is queued while it is null.
static std::atomic<mqtt_js_message_filter_t> g_js_message_filter{nullptr};

namespace {

// --- MQTT task -> loop task hand-off -----------------------------------------
//
// Why a queue and not the JS engine lock: an MQTT task that waits for script
// code (a JS timer can run for a long time on the loop task) stops reading
// its socket, and during the retained-message burst after connect the unread
// data piles up in lwIP / WiFi RX buffers until the heap runs out. So the
// MQTT task only copies what a JS subscription wants into this ring and
// returns. When script code is slower than the message rate, the ring fills
// and further messages are dropped (counted, logged by the loop task) - the
// heap and the MQTT task are unaffected.
//
// Static RAM: kQueueBytes + 8 B ring state + one StaticSemaphore_t. No heap.
// A record is 6 B + topic + payload; esp-mqtt delivers at most
// CONFIG_MQTT_BUFFER_SIZE (512 B) per message, so the ring holds at least 3
// of the largest messages and ~20-30 typical display updates.
constexpr size_t kQueueBytes = 2048;
// Records delivered per loop() pass, so timers and the REPL keep running
// while messages stream in.
constexpr int kMaxRecordsPerPass = 8;
// The MQTT task waits at most this long for the JS MQTT lock (held only for
// a table update or one copy); on timeout the message is dropped and counted.
constexpr TickType_t kMqttTaskLockWait = pdMS_TO_TICKS(5);

enum RecordKind : uint8_t { kMessage = 0, kConnected = 1, kDisconnected = 2 };

freebutton::MessageRing<kQueueBytes> g_queue;
std::atomic<uint32_t> g_dropped{0};

SemaphoreHandle_t queueLock() {
    static StaticSemaphore_t buffer;
    static SemaphoreHandle_t handle = xSemaphoreCreateMutexStatic(&buffer);
    return handle;
}

// MQTT task. Never waits for script code.
void enqueue(const RecordKind kind, const uint8_t brokerId, const std::string_view topic,
             const std::string_view payload) {
    const mqtt_js_message_filter_t filter = g_js_message_filter.load(std::memory_order_acquire);
    if (!filter) {
        return;  // no script uses MQTT
    }
    if (xSemaphoreTake(queueLock(), kMqttTaskLockWait) != pdTRUE) {
        g_dropped.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const bool wanted = kind != kMessage || filter(brokerId, topic.data(), topic.size());
    const bool queued = !wanted || g_queue.push(kind, brokerId, topic, payload);
    xSemaphoreGive(queueLock());
    if (!queued) {
        g_dropped.fetch_add(1, std::memory_order_relaxed);
    }
}

// Loop task. Rate-limited so a sustained overload does not flood the log.
void reportDrops() {
    static uint32_t reported = 0;
    static TickType_t lastLog = 0;
    const uint32_t dropped = g_dropped.load(std::memory_order_relaxed);
    if (dropped == reported) {
        return;
    }
    const TickType_t now = xTaskGetTickCount();
    if (reported != 0 && now - lastLog < pdMS_TO_TICKS(5000)) {
        return;
    }
    ESP_LOGW(TAG, "JS MQTT queue full or busy: dropped %lu event(s) (%lu total)",
             static_cast<unsigned long>(dropped - reported), static_cast<unsigned long>(dropped));
    reported = dropped;
    lastLog = now;
}

} // namespace

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

void mqtt_binding_register_message_filter(mqtt_js_message_filter_t filter) {
    g_js_message_filter.store(filter, std::memory_order_release);
}

void mqtt_binding_lock(void) {
    xSemaphoreTake(queueLock(), portMAX_DELAY);
}

void mqtt_binding_unlock(void) {
    xSemaphoreGive(queueLock());
}

} // extern "C"

/*
 * MQTT task side: queue only. Loop task side: deliver to JavaScript.
 */

namespace MqttBinding {

void notifyMessageReceived(uint8_t brokerId, const char* topic, size_t topic_len,
                           const char* payload, size_t length) {
    enqueue(kMessage, brokerId, std::string_view(topic, topic_len), std::string_view(payload, length));
}

void notifyConnected(uint8_t brokerId) {
    enqueue(kConnected, brokerId, {}, {});
}

void notifyDisconnected(uint8_t brokerId) {
    enqueue(kDisconnected, brokerId, {}, {});
}

void processPending() {
    reportDrops();

    mqtt_binding_lock();
    const bool empty = g_queue.empty();
    mqtt_binding_unlock();
    if (empty) {
        return;
    }

    // Lock order: engine lock, then (briefly, never across a call into
    // script code) the JS MQTT lock.
    const Scripting::EngineLock engine;
    for (int n = 0; n < kMaxRecordsPerPass; ++n) {
        freebutton::MessageRing<kQueueBytes>::Record record;
        mqtt_binding_lock();
        const bool have = g_queue.front(record);
        mqtt_binding_unlock();
        if (!have) {
            break;
        }

        // The record stays reserved (MQTT tasks cannot overwrite it) until
        // pop(), so script code reads topic and payload in place.
        switch (record.kind) {
            case kMessage:
                if (g_js_message_callback) {
                    ESP_LOGD(TAG, "Notifying JS: message on broker %d topic %.*s", record.channel,
                             (int)record.topic.size(), record.topic.data());
                    g_js_message_callback(record.channel, record.topic.data(), record.topic.size(),
                                          record.payload.data(), record.payload.size());
                }
                break;
            case kConnected:
                if (g_js_connect_callback) {
                    ESP_LOGI(TAG, "Notifying JS: broker %d connected", record.channel);
                    g_js_connect_callback(record.channel);
                }
                break;
            case kDisconnected:
                if (g_js_disconnect_callback) {
                    ESP_LOGI(TAG, "Notifying JS: broker %d disconnected", record.channel);
                    g_js_disconnect_callback(record.channel);
                }
                break;
            default:
                break;
        }

        mqtt_binding_lock();
        g_queue.pop();
        mqtt_binding_unlock();
    }
}

} // namespace MqttBinding
