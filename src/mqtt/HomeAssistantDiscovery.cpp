//
// Home Assistant Discovery Support
// Created by Bolke de Bruin on 24/08/2025.
//

#include "HomeAssistantDiscovery.h"

#include <atomic>
#include <cstring>

#include "Component.h"
#include "Registry.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

static auto TAG = "ha_discovery";

namespace {

using freebutton::ha::DiscoveryDevice;
using freebutton::ha::DiscoveryOut;
using freebutton::ha::DiscoverySource;

// Listens to Home Assistant's birth message (<prefix>/status). On "online",
// all discovery is republished after a random 0-5 s delay so a fleet of
// devices does not hit Home Assistant at the same instant.
class HaStatusSubscriber final : public MqttSubscriber {
public:
    void onMessage(const uint8_t brokerId, std::string_view topic, const std::string_view data) override {
        if (data == "online") {
            const uint32_t delayMs = esp_random() % 5001;
            ESP_LOGI(TAG, "Home Assistant online on broker %d, republishing discovery in %lu ms",
                     brokerId, static_cast<unsigned long>(delayMs));
            HomeAssistantDiscovery::scheduleRepublish(brokerId, delayMs);
        }
    }
    const std::string getComponentType() const override { return "ha_status"; }
};

HaStatusSubscriber& statusSubscriber() {
    static HaStatusSubscriber subscriber;
    return subscriber;
}

// --- Limits -------------------------------------------------------------------

// The largest config is a light: ~720 B with a 20-character device name
// (both the name and its slug are in it). 1024 leaves ~150 characters of name.
// Topic and payload buffers live on the polling task's stack (Arduino
// loopTask, 8 KB) only while one entry is written and published.
constexpr size_t kPayloadCapacity = 1024;
constexpr size_t kTopicCapacity = 128;
constexpr size_t kPrefixCapacity = 32;
constexpr size_t kMaxSources = 4;

// Pacing. This firmware runs small lwIP/MQTT buffers (TCP send buffer
// 2880 B, MQTT buffer 512 B); a burst of ~40 QoS 1 configs once stalled a
// socket write past esp-mqtt's network timeout. A config is published only
// while the outbox (unacknowledged QoS 1 data) is at most this size.
constexpr int kOutboxLowWaterBytes = 1024;
// After a connect, start only when the outbox has been empty this long
// (resubscriptions and retained messages arrive first).
constexpr uint32_t kSettleMs = 1000;
// Publish only when the largest free heap block holds the message
// (esp-mqtt copies it into its outbox) plus this reserve.
constexpr size_t kHeapReserveBytes = 2048;
constexpr uint32_t kHeapRetryMs = 100;
// Enqueue failures (esp-mqtt out of memory / outbox full) retry the failed
// entry after 1, 2, 4, 8, 16 s; then wait for the next trigger.
constexpr uint8_t kMaxRetries = 5;
constexpr uint32_t kRetryBaseMs = 1000;

// --- State ----------------------------------------------------------------------

struct BrokerState {
    std::atomic<bool> enabled{false};
    // Everything below is guarded by stateLock.
    bool pending = false;       // a round is scheduled or running
    bool settle = false;        // wait for the connect burst to settle first
    bool quiet = false;         // outbox seen empty since quietSince
    bool gateLogged = false;    // heap-gate warning logged this round
    uint8_t retries = 0;        // enqueue-failure retries this round
    uint16_t cursor = 0;        // next entry of the round
    uint16_t published = 0;     // messages published this round
    uint32_t epoch = 0;         // bumped by every request (restarts the round)
    TickType_t due = 0;         // earliest tick for the next step
    TickType_t quietSince = 0;
    char prefix[kPrefixCapacity] = "homeassistant";
};

BrokerState brokerStates[HomeAssistantDiscovery::kMaxBrokers];
// Short critical sections only, never across a call. Requests come from the
// MQTT task (connect, birth) and main; poll() runs in loop().
portMUX_TYPE stateLock = portMUX_INITIALIZER_UNLOCKED;

// Registered once from main(); read by poll(). Append-only: the pointer is
// stored before the count is published.
const DiscoverySource* sources[kMaxSources] = {};
std::atomic<size_t> sourceCount{0};

// Device information; only the name changes after boot (config load), so it
// is guarded by deviceMutex() while an entry is written.
HomeAssistantDevice deviceInfo;

SemaphoreHandle_t deviceMutex() {
    static StaticSemaphore_t storage;
    static SemaphoreHandle_t mutex = xSemaphoreCreateMutexStatic(&storage);
    return mutex;
}

class DeviceLock {
public:
    DeviceLock() { xSemaphoreTake(deviceMutex(), portMAX_DELAY); }
    ~DeviceLock() { xSemaphoreGive(deviceMutex()); }
    DeviceLock(const DeviceLock&) = delete;
    DeviceLock& operator=(const DeviceLock&) = delete;
};

bool reached(const TickType_t due, const TickType_t now) {
    return static_cast<int32_t>(now - due) >= 0;
}

void request(const uint8_t brokerId, const uint32_t delayMs, const bool settleFirst) {
    if (brokerId >= HomeAssistantDiscovery::kMaxBrokers) {
        ESP_LOGW(TAG, "Broker id %d out of range for discovery", brokerId);
        return;
    }
    auto& state = brokerStates[brokerId];
    if (!state.enabled.load()) {
        return;
    }
    const TickType_t due = xTaskGetTickCount() + pdMS_TO_TICKS(delayMs);
    bool coalesced;
    portENTER_CRITICAL(&stateLock);
    coalesced = state.pending;
    if (!state.pending || static_cast<int32_t>(due - state.due) < 0) {
        state.due = due;  // earliest request wins
    }
    state.pending = true;
    state.cursor = 0;     // a new request always publishes everything
    state.published = 0;
    state.retries = 0;
    state.gateLogged = false;
    ++state.epoch;
    if (settleFirst) {
        state.settle = true;
        state.quiet = false;
    }
    portEXIT_CRITICAL(&stateLock);

    if (coalesced) {
        ESP_LOGD(TAG, "Discovery for broker %d already pending, coalesced", brokerId);
    }
}

// Ends the round of `epoch` (unless a newer request restarted it).
bool endRound(const uint8_t brokerId, const uint32_t epoch, uint16_t& published) {
    bool ended = false;
    portENTER_CRITICAL(&stateLock);
    auto& state = brokerStates[brokerId];
    if (state.epoch == epoch) {
        state.pending = false;
        state.retries = 0;
        published = state.published;
        ended = true;
    }
    portEXIT_CRITICAL(&stateLock);
    return ended;
}

// Enqueue of entry `failedAt` failed: retry it later, bounded.
void retryLater(const uint8_t brokerId, const uint32_t epoch, const uint16_t failedAt) {
    uint8_t attempt = 0;
    bool superseded = false;
    uint32_t delayMs = 0;
    portENTER_CRITICAL(&stateLock);
    auto& state = brokerStates[brokerId];
    if (state.epoch != epoch) {
        superseded = true;  // a new round was requested meanwhile
    } else if (state.retries < kMaxRetries) {
        attempt = ++state.retries;
        delayMs = kRetryBaseMs << (attempt - 1);
        state.cursor = failedAt;
        state.due = xTaskGetTickCount() + pdMS_TO_TICKS(delayMs);
    } else {
        state.pending = false;
        state.retries = 0;
    }
    portEXIT_CRITICAL(&stateLock);

    if (superseded) {
        ESP_LOGW(TAG, "Broker %d: discovery enqueue failed at entry %u, new round already requested",
                 brokerId, static_cast<unsigned>(failedAt));
    } else if (attempt > 0) {
        ESP_LOGW(TAG, "Broker %d: discovery enqueue failed at entry %u, retry %u/%u in %lu ms",
                 brokerId, static_cast<unsigned>(failedAt), static_cast<unsigned>(attempt),
                 static_cast<unsigned>(kMaxRetries), static_cast<unsigned long>(delayMs));
    } else {
        ESP_LOGE(TAG, "Broker %d: discovery enqueue failed at entry %u after %u retries; "
                 "giving up until the next reconnect / Home Assistant birth", brokerId,
                 static_cast<unsigned>(failedAt), static_cast<unsigned>(kMaxRetries));
    }
}

// One step of a broker's round. true: a message was handed to esp-mqtt
// (successfully or not), so poll() is done for this call.
bool step(const uint8_t brokerId) {
    auto& state = brokerStates[brokerId];
    const TickType_t now = xTaskGetTickCount();

    char prefix[kPrefixCapacity];
    uint32_t epoch;
    uint16_t cursor;
    bool settle;
    bool due;
    portENTER_CRITICAL(&stateLock);
    due = state.pending && reached(state.due, now);
    epoch = state.epoch;
    cursor = state.cursor;
    settle = state.settle;
    if (due) {
        std::memcpy(prefix, state.prefix, sizeof(prefix));
    }
    portEXIT_CRITICAL(&stateLock);
    if (!due) {
        return false;
    }

    uint16_t published = 0;
    auto* broker = MqttBrokerRegistry::getBroker(brokerId);
    if (!state.enabled.load() || !broker || !broker->isConnected()) {
        if (endRound(brokerId, epoch, published)) {
            ESP_LOGI(TAG, "Broker %d not connected, discovery round dropped (connect reschedules)", brokerId);
        }
        return false;
    }

    const int outbox = broker->outboxSize();
    if (settle) {
        bool settled = false;
        portENTER_CRITICAL(&stateLock);
        if (state.epoch == epoch) {
            if (outbox != 0) {
                state.quiet = false;
            } else if (!state.quiet) {
                state.quiet = true;
                state.quietSince = now;
            } else if (now - state.quietSince >= pdMS_TO_TICKS(kSettleMs)) {
                state.settle = false;
                settled = true;
            }
        }
        portEXIT_CRITICAL(&stateLock);
        if (!settled) {
            return false;
        }
        ESP_LOGI(TAG, "Broker %d: connect burst settled, publishing discovery", brokerId);
    }
    if (outbox > kOutboxLowWaterBytes) {
        return false;
    }

    // Find the next entry with something to publish, from the cursor on.
    char topic[kTopicCapacity];
    char payload[kPayloadCapacity];
    size_t topicLength = 0;
    size_t payloadLength = 0;
    size_t entry = cursor;
    bool found = false;
    size_t first = 0;  // flat index of the current source's entry 0
    const size_t count = sourceCount.load(std::memory_order_acquire);
    for (size_t s = 0; s < count && !found; ++s) {
        const DiscoverySource* source = sources[s];
        const size_t n = source->discoveryCount();
        for (; entry < first + n; ++entry) {
            DeviceLock lock;
            const DiscoveryDevice device{deviceInfo.name, deviceInfo.manufacturer, deviceInfo.model,
                                         deviceInfo.sw_version, deviceInfo.identifier};
            DiscoveryOut out(topic, sizeof(topic), payload, sizeof(payload), prefix, device);
            if (source->writeDiscovery(entry - first, brokerId, out) && out.finish(topicLength, payloadLength)) {
                found = true;
                break;
            }
            if (out.failed()) {
                ESP_LOGE(TAG, "Discovery entry %u (source %u #%u) skipped: does not fit (topic %u B, "
                         "payload %u B), or a name / topic was cut or missing",
                         static_cast<unsigned>(entry), static_cast<unsigned>(s),
                         static_cast<unsigned>(entry - first), static_cast<unsigned>(kTopicCapacity),
                         static_cast<unsigned>(kPayloadCapacity));
            }
        }
        first += n;
    }

    if (!found) {
        if (endRound(brokerId, epoch, published)) {
            ESP_LOGI(TAG, "Discovery for broker %d done: %u messages", brokerId, static_cast<unsigned>(published));
        }
        return false;
    }

    // esp-mqtt copies the message into its outbox: only with room to spare.
    const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    if (largest < payloadLength + kHeapReserveBytes) {
        bool log = false;
        portENTER_CRITICAL(&stateLock);
        if (state.epoch == epoch) {
            state.cursor = static_cast<uint16_t>(entry);
            state.due = now + pdMS_TO_TICKS(kHeapRetryMs);
            log = !state.gateLogged;
            state.gateLogged = true;
        }
        portEXIT_CRITICAL(&stateLock);
        if (log) {
            ESP_LOGW(TAG, "Broker %d: discovery waits for heap (largest block %u B, need %u B)", brokerId,
                     static_cast<unsigned>(largest), static_cast<unsigned>(payloadLength + kHeapReserveBytes));
        }
        return false;
    }

    ESP_LOGD(TAG, "%s discovery at %s", payloadLength ? "Publishing" : "Clearing", topic);
    if (broker->publish(topic, payload, payloadLength, 1, true) < 0) {
        retryLater(brokerId, epoch, static_cast<uint16_t>(entry));
        return true;
    }

    portENTER_CRITICAL(&stateLock);
    if (state.epoch == epoch) {
        state.cursor = static_cast<uint16_t>(entry + 1);
        ++state.published;
    }
    portEXIT_CRITICAL(&stateLock);
    return true;
}

} // namespace

void HomeAssistantDiscovery::configureBroker(const uint8_t brokerId, const bool enabled, const std::string& discovery_prefix) {
    if (brokerId >= kMaxBrokers) {
        ESP_LOGW(TAG, "Broker id %d out of range for discovery (max %d)", brokerId, kMaxBrokers - 1);
        return;
    }
    auto& state = brokerStates[brokerId];
    if (discovery_prefix.empty() || discovery_prefix.size() >= kPrefixCapacity) {
        ESP_LOGE(TAG, "Discovery prefix '%s' invalid (1..%u chars), discovery disabled for broker %d",
                 discovery_prefix.c_str(), static_cast<unsigned>(kPrefixCapacity - 1), brokerId);
        state.enabled.store(false);
        return;
    }

    portENTER_CRITICAL(&stateLock);
    std::memcpy(state.prefix, discovery_prefix.c_str(), discovery_prefix.size() + 1);
    portEXIT_CRITICAL(&stateLock);
    state.enabled.store(enabled);

    if (enabled) {
        statusSubscriber().addSubscription(brokerId, discovery_prefix + "/status", 1);
    }

    ESP_LOGI(TAG, "Configured discovery for broker %d: %s (prefix: %s)",
             brokerId, enabled ? "enabled" : "disabled", discovery_prefix.c_str());
}

bool HomeAssistantDiscovery::isDiscoveryEnabled(const uint8_t brokerId) {
    return brokerId < kMaxBrokers && brokerStates[brokerId].enabled.load();
}

void HomeAssistantDiscovery::addSource(const DiscoverySource& source) {
    const size_t n = sourceCount.load(std::memory_order_relaxed);
    for (size_t i = 0; i < n; ++i) {
        if (sources[i] == &source) {
            return;
        }
    }
    if (n >= kMaxSources) {
        ESP_LOGE(TAG, "Too many discovery sources (max %u)", static_cast<unsigned>(kMaxSources));
        return;
    }
    sources[n] = &source;
    sourceCount.store(n + 1, std::memory_order_release);
    ESP_LOGI(TAG, "Registered discovery source %u (%u entries)", static_cast<unsigned>(n),
             static_cast<unsigned>(source.discoveryCount()));

    // Registration may follow the connect; coalesces with its request.
    for (uint8_t id = 0; id < kMaxBrokers; ++id) {
        if (isDiscoveryEnabled(id)) {
            request(id, 300, false);
        }
    }
}

void HomeAssistantDiscovery::setDeviceInfo(const HomeAssistantDevice& device) {
    {
        DeviceLock lock;
        deviceInfo = device;
    }
    ESP_LOGI(TAG, "Set global device info: %s (%.*s)", device.name.c_str(),
             static_cast<int>(device.identifier.size()), device.identifier.data());
}

void HomeAssistantDiscovery::setDeviceName(std::string name) {
    DeviceLock lock;
    deviceInfo.name = std::move(name);
}

void HomeAssistantDiscovery::onBrokerConnected(const uint8_t brokerId) {
    // Runs in the MQTT task (MQTT_EVENT_CONNECTED), which must stay free to
    // drain the outbox: only mark the round; poll() publishes it once the
    // connect burst has settled.
    ESP_LOGI(TAG, "Broker %d connected, discovery starts once the connect burst settles", brokerId);
    request(brokerId, 0, true);
}

void HomeAssistantDiscovery::scheduleRepublish(const uint8_t brokerId, const uint32_t delayMs) {
    request(brokerId, delayMs, false);
}

void HomeAssistantDiscovery::poll() {
    // Round-robin over brokers; at most one message per call.
    static uint8_t next = 0;
    for (uint8_t k = 0; k < kMaxBrokers; ++k) {
        const uint8_t id = static_cast<uint8_t>((next + k) % kMaxBrokers);
        if (step(id)) {
            next = static_cast<uint8_t>((id + 1) % kMaxBrokers);
            return;
        }
    }
}
