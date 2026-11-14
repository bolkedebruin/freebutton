//
// Created by Bolke de Bruin on 10/08/2025.
//
#include "Broker.h"
#include "Registry.h"

#include <algorithm>
#include <cstring>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "SubscribePlan.h"

// Import JavaScript MQTT binding layer
#include "../scripting/mqtt_binding.h"

static const auto TAG = "mqtt";

namespace {

// esp-mqtt builds every SUBSCRIBE in its out-buffer. start() sets neither
// buffer.size nor buffer.out_size, so that is CONFIG_MQTT_BUFFER_SIZE
// (esp-mqtt's own default is 1024 when the option is absent).
#ifdef CONFIG_MQTT_BUFFER_SIZE
constexpr size_t kSubscribeBufferBytes = CONFIG_MQTT_BUFFER_SIZE;
#else
constexpr size_t kSubscribeBufferBytes = 1024;
#endif

// Topics per SUBSCRIBE. Each topic can make the broker send a retained message
// right after the SUBACK, so this also bounds the retained burst per batch.
// Topics here are ~40-50 chars (45-56 B encoded): 8 take ~450 B, inside the
// 512 B buffer. Longer topics are split by encoded size instead.
constexpr size_t kMaxTopicsPerBatch = 8;

// If a batch's SUBACK has not arrived after this long, the next MQTT event
// sends the next batch anyway. esp-mqtt itself retransmits the SUBSCRIBE
// every second (message_retransmit_timeout) until the outbox entry expires.
constexpr uint32_t kSubackTimeoutMs = 3000;

// Outbox cap: bytes of unacknowledged QoS>0 PUBLISH and SUBSCRIBE packets.
// - Home Assistant discovery is paced: it publishes the next config only once
//   the outbox is <= 1024 B, so its steady state is 1024 B + one config.
// - A resubscribe batch adds at most one SUBSCRIBE of <= 512 B (the
//   out-buffer). (The birth message is QoS 0 and not counted.)
// - The lwIP TCP send buffer is 2880 B: more than ~3 KB unacknowledged means
//   the link is stalled, and queueing more only spends heap.
// 3 KB covers 1024 B + a discovery config + a SUBSCRIBE batch. Beyond it,
// QoS>0 publish and subscribe return -2 (logged) instead of allocating.
constexpr uint64_t kOutboxLimitBytes = 3 * 1024;

constexpr uint8_t clampQos(const int qos) {
    return static_cast<uint8_t>(qos < 0 ? 0 : (qos > 2 ? 2 : qos));
}


// Extra subscription slots reserved in start(), on top of what was registered
// before it (config buttons/display items and the HA status topic, which
// configureBroker() adds before connectAll()). Subscriptions added after
// start():
//   - one Home Assistant command topic per button (subscribeHomeAssistantCommands)
//   - the JS layer, at most MAX_SUBSCRIPTIONS_PER_BROKER (8) per broker
//     (freebutton_mqtt.c)
// LEDs and sensors subscribe to nothing. subscribe() warns if the list ever
// grows past this, instead of silently reallocating during the connect burst.
constexpr size_t kJsSubscriptionsPerBroker = 8;
constexpr size_t kReservedExtraSubscriptions = BUTTON_COUNT + kJsSubscriptionsPerBroker;

constexpr uint32_t kWarnIntervalMs = 1000;

bool rateLimitElapsed(uint32_t& lastTick) {
    const TickType_t now = xTaskGetTickCount();
    if (lastTick != 0 && (now - lastTick) < pdMS_TO_TICKS(kWarnIntervalMs)) {
        return false;
    }
    lastTick = now == 0 ? 1 : now;
    return true;
}

// RAII guard for MqttBroker::subscriptionsLock_ (recursive: the MQTT task
// re-enters subscribe()/unsubscribe() from JS and component callbacks).
class SubscriptionsLock {
public:
    explicit SubscriptionsLock(SemaphoreHandle_t lock) : lock_(lock) { xSemaphoreTakeRecursive(lock_, portMAX_DELAY); }
    ~SubscriptionsLock() { xSemaphoreGiveRecursive(lock_); }
    SubscriptionsLock(const SubscriptionsLock&) = delete;
    SubscriptionsLock& operator=(const SubscriptionsLock&) = delete;

private:
    SemaphoreHandle_t lock_;
};

} // namespace

void MqttBroker::start() {
    if (url.empty()) {
        ESP_LOGE(TAG, "MQTT URL is empty, cannot start broker");
        return;
    }

    std::ranges::transform(url, url.begin(), ::tolower);

    if (url.find("mqtt://") == 0 || url.find("ws://") == 0 ||
        url.find("wss://") == 0 || url.find("mqtts://") == 0) {

        {
            // Size the list once, before the connect burst (see kReservedExtraSubscriptions).
            SubscriptionsLock lock(subscriptionsLock_);
            subscriptions_.reserve(subscriptions_.size() + kReservedExtraSubscriptions);
        }

        esp_mqtt_client_config_t mqtt_cfg = {};
        mqtt_cfg.broker.address.uri = url.c_str();
        mqtt_cfg.broker.address.port = port > 0 ? port : (url.find("ws://") == 0 || url.find("wss://") == 0 ? 80 : 1883);
        mqtt_cfg.credentials.username = username.c_str();
        mqtt_cfg.credentials.authentication.password = password.c_str();
        mqtt_cfg.session.last_will.topic = statusTopic_.c_str();
        mqtt_cfg.session.last_will.msg = "offline";
        mqtt_cfg.session.last_will.qos = 1;
        mqtt_cfg.session.last_will.retain = 1;
        mqtt_cfg.outbox.limit = kOutboxLimitBytes;
        // The SUBSCRIBE planner sizes batches for this buffer; pin it so a
        // later buffer.size change cannot make every batch fail with -1.
        mqtt_cfg.buffer.out_size = static_cast<int>(kSubscribeBufferBytes);

        client = esp_mqtt_client_init(&mqtt_cfg);
        ESP_LOGI(TAG, "Registering MQTT event handler for broker '%.*s' with ID %d", (int)name.length(), name.data(), brokerId);
        esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, mqtt_event_handler, reinterpret_cast<void*>(static_cast<uintptr_t>(brokerId)));
        if (esp_mqtt_client_start(client) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start MQTT client");
            esp_mqtt_client_destroy(client);
            client = nullptr;
        } else {
            enabled = true;
        }
    }
}

void MqttBroker::stop() {
    if (client) {
        // Unregister event handler first to prevent callbacks after destruction
        esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, nullptr, nullptr);
        esp_mqtt_client_stop(client);
        esp_mqtt_client_destroy(client);
        client = nullptr;
        enabled = false;
        connected_ = false;
    }
}

void MqttBroker::sendBirth() const {
    // QoS 0 retained: QoS 0 is not subject to outbox.limit, so the birth can
    // never be refused with -2 (stale outbox items survive a reconnect) and
    // leave the retained LWT "offline" in place.
    // Zero-copy: statusTopic_ is NUL-terminated and built once.
    publish(statusTopic_.c_str(), "online", 6, 0, 1);
}

void MqttBroker::publish(std::string_view topic, std::string_view data, const int qos, const int retain) const {
    if (topic.empty()) {
        ESP_LOGE(TAG, "Empty topic (data: %.*s)", data.length(), data.data());
        return;
    }

    ESP_LOGD(TAG, "topic: %.*s data: %.*s", topic.length(), topic.data(), data.length(), data.data());

    if (enabled) {
        // Only the topic needs NUL termination (small stack copy, no heap);
        // the payload goes by explicit length, so it is not copied here.
        char topic_buf[topic.length() + 1];
        std::memcpy(topic_buf, topic.data(), topic.length());
        topic_buf[topic.length()] = '\0';

        // len 0 would make esp-mqtt strlen() the payload, which a string_view
        // need not terminate: send an empty payload as "" explicitly.
        const char* payload = data.empty() ? "" : data.data();
        if (int ret = esp_mqtt_client_publish(client, topic_buf, payload, static_cast<int>(data.length()), qos, retain);
            ret < 0) {
            ESP_LOGW(TAG, "Failed to enqueue message: %d", ret);
        }
    }
}

int MqttBroker::publish(const char* topic, const char* payload, const size_t length, const int qos,
                        const int retain) const {
    if (!topic || topic[0] == '\0') {
        ESP_LOGE(TAG, "Empty topic (zero-copy publish)");
        return -1;
    }
    if (!payload) {
        payload = "";
    }

    ESP_LOGD(TAG, "topic: %s (%u B, zero-copy)", topic, static_cast<unsigned>(length));

    if (!enabled || !client) {
        return -1;
    }
    // len 0 makes esp-mqtt take strlen(payload): payload is NUL-terminated,
    // so an empty payload stays empty.
    const int ret = esp_mqtt_client_publish(client, topic, payload, static_cast<int>(length), qos, retain);
    if (ret < 0) {
        ESP_LOGW(TAG, "Failed to enqueue message on %s: %d", topic, ret);
    }
    return ret;
}

// --- Subscription list ------------------------------------------------------
//
// subscribe()/unsubscribe() may run in any task (main, loop/JS, component
// destructors, or the MQTT task re-entering from a callback). They only edit
// subscriptions_ under subscriptionsLock_ and then wake the MQTT task. The
// MQTT task (pumpSubscriptions) sends every SUBSCRIBE/UNSUBSCRIBE, so failed
// sends (-1, -2 outbox full) are retried there instead of being lost.
//
// Lock order: the MQTT task runs event handlers with esp-mqtt's API lock
// held and then takes subscriptionsLock_. Other tasks therefore never call
// an esp-mqtt API while holding subscriptionsLock_. Subscriber callbacks run
// under subscriptionsLock_ (dispatchMessage), so onMessage must not take any
// other lock either.

void MqttBroker::subscribe(std::string_view topic, const int qos, MqttSubscriber *subscriber) {
    // Always store subscription regardless of client state - will subscribe when client is ready

    if (topic.empty()) {
        ESP_LOGE(TAG, "Empty topic for subscription");
        return;
    }

    ESP_LOGD(TAG, "Subscribing to topic: %.*s", topic.length(), topic.data());

    {
        SubscriptionsLock lock(subscriptionsLock_);

        auto it = std::ranges::find_if(subscriptions_,
                                       [topic](const SubscriptionEntry& entry) { return entry.topic == topic; });

        if (it != subscriptions_.end()) {
            // Topic already exists, add subscriber if not already present
            if (std::ranges::find(it->subscribers, subscriber) == it->subscribers.end()) {
                // An entry without subscribers is a pending UNSUBSCRIBE; adding
                // one cancels it. The topic is subscribed again all the same:
                // the broker then sends its retained message to the new
                // subscriber, as it would have after the UNSUBSCRIBE (a
                // restarted script subscribes to the topics of the old one).
                if (it->subscribers.empty()) {
                    it->sent = false;
                }
                it->subscribers.push_back(subscriber);
            } else {
                ESP_LOGW(TAG, "Subscriber already registered for topic: %.*s", (int)topic.length(), topic.data());
            }
            // Keep the highest requested QoS; the broker sees it on the next (re)subscribe.
            it->qos = std::max(it->qos, clampQos(qos));
            if (it->sent) {
                return;
            }
        } else {
            if (subscriptions_.size() == subscriptions_.capacity()) {
                ESP_LOGW(TAG, "Subscription list grows past its reserved %u entries",
                         static_cast<unsigned>(subscriptions_.capacity()));
            }
            // Construct in place (the topic string is the only allocation).
            subscriptions_.emplace_back(topic, clampQos(qos), subscriber);
        }
        subscriptionWork_ = true;
    }

    requestSubscriptionWork();
}

void MqttBroker::unsubscribe(std::string_view topic) {
    if (topic.empty()) {
        ESP_LOGE(TAG, "Empty topic for unsubscription");
        return;
    }

    ESP_LOGD(TAG, "Unsubscribing from topic: %.*s", topic.length(), topic.data());

    {
        SubscriptionsLock lock(subscriptionsLock_);

        auto it = std::ranges::find_if(subscriptions_,
                                       [topic](const SubscriptionEntry& entry) { return entry.topic == topic; });
        if (it == subscriptions_.end()) {
            ESP_LOGD(TAG, "Not subscribed to topic: %.*s", (int)topic.length(), topic.data());
            return;
        }

        if (!(it->sent && connected_.load())) {
            // Never reached the broker on this connection: just forget it.
            subscriptions_.erase(it);
            return;
        }
        // Leave it as a pending UNSUBSCRIBE for the MQTT task.
        it->subscribers.clear();
        subscriptionWork_ = true;
    }

    requestSubscriptionWork();
}

void MqttBroker::unsubscribe(MqttSubscriber* subscriber) {
    if (!subscriber) {
        ESP_LOGE(TAG, "Cannot unsubscribe null subscriber");
        return;
    }

    bool wake = false;
    {
        SubscriptionsLock lock(subscriptionsLock_);
        const bool online = connected_.load();

        // Remove subscriber from all topics and clean up empty topics
        for (auto it = subscriptions_.begin(); it != subscriptions_.end();) {
            auto& subscribers = it->subscribers;
            if (std::erase(subscribers, subscriber) == 0 || !subscribers.empty()) {
                ++it;
                continue;
            }
            // No subscribers left: unsubscribe at the broker (MQTT task) or just forget it.
            if (it->sent && online) {
                ESP_LOGD(TAG, "No more subscribers for topic %s, unsubscribing from broker", it->topic.c_str());
                subscriptionWork_ = true;
                wake = true;
                ++it;
            } else {
                it = subscriptions_.erase(it);
            }
        }
    }

    if (wake) {
        requestSubscriptionWork();
    }
}

// Wake the MQTT task so pumpSubscriptions() runs soon. Without a wake-up the
// work still runs on the next MQTT event of any kind.
void MqttBroker::requestSubscriptionWork() {
    if (!client || !connected_.load()) {
        return;  // CONNECTED resubscribes everything
    }
    if (xTaskGetCurrentTaskHandle() == mqttTask_.load()) {
        return;  // already in the MQTT task: the pump runs at the end of this event
    }
#if defined(CONFIG_MQTT_EVENT_QUEUE_SIZE) && CONFIG_MQTT_EVENT_QUEUE_SIZE > 1
    // One wake-up in flight at most. esp-mqtt posts its own events with
    // portMAX_DELAY into the same queue, so the queue must have room for
    // them besides this one (hence CONFIG_MQTT_EVENT_QUEUE_SIZE > 1).
    if (wakePosted_.exchange(true)) {
        return;
    }
    esp_mqtt_event_t wake = {};
    wake.event_id = MQTT_USER_EVENT;
    if (esp_mqtt_dispatch_custom_event(client, &wake) != ESP_OK) {
        wakePosted_ = false;
        ESP_LOGD(TAG, "Could not wake the MQTT task; subscription work runs on its next event");
    }
#else
#warning "CONFIG_MQTT_EVENT_QUEUE_SIZE <= 1: subscribe() from other tasks waits for the next MQTT event"
#endif
}

void MqttBroker::mqtt_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data) {
    const auto brokerId = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(handler_args));

    auto* event = static_cast<esp_mqtt_event_handle_t>(event_data);
    if (!event) {
        ESP_LOGE(TAG, "NULL event data in event handler");
        return;
    }

    ESP_LOGD(TAG, "Event handler called for broker ID %d, event type %d", brokerId, event_id);

    // Get broker from registry using the broker ID
    MqttBroker* broker = MqttBrokerRegistry::getBroker(brokerId);
    if (!broker) {
        ESP_LOGE(TAG, "Broker with ID %d not found in registry", brokerId);
        return;
    }

    ESP_LOGD(TAG, "Found broker %p for ID %d, calling handleMqttEvent", (void*)broker, brokerId);
    broker->handleMqttEvent(event);
}

void MqttBroker::handleMqttEvent(esp_mqtt_event_handle_t event) const {
    // Runs in the MQTT task (with esp-mqtt's API lock held).
    auto* self = const_cast<MqttBroker*>(this);
    self->mqttTask_ = xTaskGetCurrentTaskHandle();

    switch (event->event_id) {
        case MQTT_EVENT_BEFORE_CONNECT:
            self->stopResubscribe();
            break;
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT %s broker connected", url.c_str());
            self->connected_ = true;
            self->wakePosted_ = false;  // a wake-up lost with a previous client must not block new ones
            sendBirth();

            // Resubscribe to all existing subscriptions. The pump below sends
            // the first batch only; the rest follow their SUBACKs.
            self->startResubscribe();

            // Queue for the JavaScript callbacks (run later on the loop task)
            MqttBinding::notifyConnected(brokerId);

            if (on_connected_callback_) {
                on_connected_callback_(brokerId);
            }
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT broker %s disconnected", url.c_str());
            self->connected_ = false;
            // Abandon a running resubscribe; the next CONNECTED starts over.
            self->stopResubscribe();

            // Queue for the JavaScript callbacks (run later on the loop task)
            MqttBinding::notifyDisconnected(brokerId);

            if (on_disconnected_callback_) {
                on_disconnected_callback_(brokerId);
            }
            break;
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGD(TAG, "Subscribed to topic (msg_id: %d)", event->msg_id);
            if (event->error_handle && event->error_handle->error_type == MQTT_ERROR_TYPE_SUBSCRIBE_FAILED) {
                ESP_LOGW(TAG, "Broker rejected topic(s) in SUBSCRIBE msg_id %d", event->msg_id);
            }
            {
                SubscriptionsLock lock(subscriptionsLock_);
                if (event->msg_id == batchMsgId_) {
                    self->batchMsgId_ = -1;  // the pump below sends the next batch
                }
            }
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGD(TAG, "Unsubscribed from topic (msg_id: %d)", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGD(TAG, "Message published (msg_id: %d)", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            dispatchMessage(event);
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT error occurred");
            if (on_error_callback_ && event->error_handle) {
                on_error_callback_(brokerId, event->error_handle);
            }
            break;
        case MQTT_USER_EVENT:
            // Wake-up from requestSubscriptionWork(); the pump below does the work.
            self->wakePosted_ = false;
            break;
        default:
            ESP_LOGW(TAG, "Unhandled MQTT event: %d", event->event_id);
            break;
    }

    // Every event advances pending subscription work (cheap no-op when idle).
    self->pumpSubscriptions();
}

void MqttBroker::dispatchMessage(esp_mqtt_event_handle_t event) const {
    // A message larger than esp-mqtt's in-buffer arrives as several DATA
    // events; only the first has the topic. Reassembly would need a heap
    // buffer, so such messages are dropped instead of being dispatched
    // truncated.
    if (event->current_data_offset != 0 || event->data_len != event->total_data_len) {
        static uint32_t lastFragmentWarnTick = 0;
        if (event->current_data_offset == 0 && rateLimitElapsed(lastFragmentWarnTick)) {
            ESP_LOGW(TAG, "Dropping %d B message on %.*s: larger than the MQTT in-buffer", event->total_data_len,
                     event->topic_len, event->topic);
        }
        return;
    }

    // Debug level: at INFO the UART write per retained message stalls the
    // MQTT task during the connect burst.
    ESP_LOGD(TAG, "Message received on topic: %.*s", event->topic_len, event->topic);

    // Zero-copy dispatch: views into esp-mqtt's in-buffer, valid for the
    // duration of this event only.
    const std::string_view topic_view(event->topic, event->topic_len);
    const std::string_view data_view(event->data, event->data_len);

    // Subscribers are called WITH subscriptionsLock_ held. A subscriber
    // unregisters itself (destructor -> unsubscribe(this)) under the same
    // lock, so it cannot be freed while it is being called.
    //
    // RULE for MqttSubscriber::onMessage (see Component.h): it must not block
    // on any other lock (no LVGL lock, no JS engine); it may only copy the
    // data into its own slots or set flags, and defer the real work to its
    // own task. Otherwise: MQTT task holds this lock and waits for the LVGL
    // lock, while a task holding the LVGL lock waits here in subscribe().
    //
    // The lock is recursive, so a callback may still subscribe/unsubscribe
    // from this task; that can move or erase entries, hence the index loop
    // that re-finds the entry every round instead of holding references.
    {
        SubscriptionsLock lock(subscriptionsLock_);
        for (size_t s = 0;; ++s) {
            MqttSubscriber* target = nullptr;
            for (const auto& subscription : subscriptions_) {
                if (subscription.topic == topic_view) {
                    if (s < subscription.subscribers.size()) {
                        target = subscription.subscribers[s];
                    }
                    break;
                }
            }
            if (!target) {
                break;
            }
            target->onMessage(brokerId, topic_view, data_view);
        }
    }

    // Outside the lock. The JS layer does not run script code here: it copies
    // the message into its bounded queue if a JS subscription matches (or
    // drops it when full) and the loop task runs the callbacks. It never
    // waits for the JS engine.
    MqttBinding::notifyMessageReceived(brokerId, event->topic, static_cast<size_t>(event->topic_len),
                                       event->data, static_cast<size_t>(event->data_len));

    // Keep existing callback for backward compatibility
    if (on_message_callback_) {
        on_message_callback_(brokerId, event->topic, event->data, event->data_len);
    }
}

// Callback setters
void MqttBroker::setOnConnectedCallback(std::function<void(uint8_t)> callback) {
    on_connected_callback_ = callback;
}

void MqttBroker::setOnDisconnectedCallback(std::function<void(uint8_t)> callback) {
    on_disconnected_callback_ = callback;
}

void MqttBroker::setOnMessageCallback(std::function<void(uint8_t, const char*, const char*, size_t)> callback) {
    on_message_callback_ = callback;
}

void MqttBroker::setOnErrorCallback(std::function<void(uint8_t, esp_mqtt_error_codes*)> callback) {
    on_error_callback_ = callback;
}

void MqttBroker::resubscribeAll() const {
    const_cast<MqttBroker*>(this)->startResubscribe();
}

// --- Batched, SUBACK-driven subscription pump --------------------------------
//
// Subscribing to every topic at once from MQTT_EVENT_CONNECTED keeps every
// SUBSCRIBE in the outbox (none can be acknowledged while the handler runs)
// while the broker streams SUBACKs and retained messages into lwIP/WiFi RX
// buffers that nobody reads. Instead: one batch per SUBACK. Between batches
// the MQTT task returns to its loop and drains the socket. No extra task: the
// pump runs at the end of every MQTT event, in the MQTT task.

void MqttBroker::startResubscribe() {
    if (!enabled || !client) {
        ESP_LOGW(TAG, "Cannot resubscribe: broker not enabled or client not initialized");
        return;
    }

    SubscriptionsLock lock(subscriptionsLock_);
    // Clean session: nothing is subscribed yet. Pending unsubscribes are moot.
    std::erase_if(subscriptions_, [](const SubscriptionEntry& entry) { return entry.subscribers.empty(); });
    for (auto& subscription : subscriptions_) {
        subscription.sent = false;
    }
    batchMsgId_ = -1;
    subscriptionWork_ = true;

    ESP_LOGI(TAG, "Resubscribing to %u topics in batches of up to %u", static_cast<unsigned>(subscriptions_.size()),
             static_cast<unsigned>(kMaxTopicsPerBatch));
}

void MqttBroker::stopResubscribe() {
    SubscriptionsLock lock(subscriptionsLock_);
    batchMsgId_ = -1;
}

// MQTT task only. Holds subscriptionsLock_ throughout: the batch's topic
// pointers reference subscriptions_ directly and must stay valid until
// esp_mqtt_client_subscribe_multiple() has copied them into its out-buffer.
void MqttBroker::pumpSubscriptions() {
    SubscriptionsLock lock(subscriptionsLock_);

    if (!client || !connected_.load()) {
        return;
    }
    if (!subscriptionWork_ && batchMsgId_ < 0) {
        return;
    }

    // 1. Pending unsubscribes. An entry without subscribers IS the
    //    "unsubscribe pending" state: it stays in the list until its
    //    UNSUBSCRIBE was handed to esp-mqtt, so a failed send is retried on
    //    the next event (subscriptionWork_ stays set). A new subscriber for
    //    the topic cancels it (subscribe()).
    bool unsubscribeRetry = false;
    for (auto it = subscriptions_.begin(); it != subscriptions_.end();) {
        if (!it->subscribers.empty()) {
            ++it;
            continue;
        }
        // Not sent on this connection, or too long to have ever been
        // subscribed (the planner skips those): nothing to undo at the broker.
        if (it->sent && freebutton::mqtt::subscribeTopicFits(it->topic.size(), kSubscribeBufferBytes)) {
            if (const int msg_id = esp_mqtt_client_unsubscribe(client, it->topic.c_str()); msg_id < 0) {
                if (rateLimitElapsed(lastUnsubscribeWarnTick_)) {
                    ESP_LOGW(TAG, "Failed to unsubscribe from topic: %s, error: %d, will retry", it->topic.c_str(),
                             msg_id);
                }
                unsubscribeRetry = true;
                ++it;
                continue;
            }
        }
        it = subscriptions_.erase(it);
    }

    // 2. One SUBSCRIBE in flight at a time.
    if (batchMsgId_ >= 0) {
        const uint32_t elapsedMs = pdTICKS_TO_MS(xTaskGetTickCount() - batchSentTick_);
        if (elapsedMs < kSubackTimeoutMs) {
            return;
        }
        ESP_LOGW(TAG, "No SUBACK for msg_id %d after %lu ms, sending next batch", batchMsgId_,
                 static_cast<unsigned long>(elapsedMs));
        batchMsgId_ = -1;
    }

    // 3. Next batch of unsent topics. ~100 B of MQTT task stack.
    esp_mqtt_topic_t batch[kMaxTopicsPerBatch];
    size_t batchIndex[kMaxTopicsPerBatch];
    const size_t count = freebutton::mqtt::planSubscribeBatch(
        subscriptions_.size(), kSubscribeBufferBytes, kMaxTopicsPerBatch,
        [this](const size_t i) { return !subscriptions_[i].sent && !subscriptions_[i].subscribers.empty(); },
        [this](const size_t i) { return subscriptions_[i].topic.size(); },
        [&](const size_t k, const size_t i) {
            batch[k] = {subscriptions_[i].topic.c_str(), subscriptions_[i].qos};
            batchIndex[k] = i;
        },
        [this](const size_t i) {
            ESP_LOGE(TAG, "Topic %s does not fit a %u B SUBSCRIBE, not subscribing", subscriptions_[i].topic.c_str(),
                     static_cast<unsigned>(kSubscribeBufferBytes));
            subscriptions_[i].sent = true;
        });

    if (count == 0) {
        if (subscriptionWork_ && !unsubscribeRetry) {
            subscriptionWork_ = false;
            ESP_LOGI(TAG, "Subscriptions up to date (%u topics)", static_cast<unsigned>(subscriptions_.size()));
        }
        return;
    }

    const int msg_id = esp_mqtt_client_subscribe_multiple(client, batch, static_cast<int>(count));
    if (msg_id < 0) {
        // Entries stay unsent; the next MQTT event retries.
        if (rateLimitElapsed(lastBatchWarnTick_)) {
            ESP_LOGW(TAG, "Failed to send SUBSCRIBE batch of %u (first %s), error: %d%s, will retry",
                     static_cast<unsigned>(count), batch[0].filter, msg_id, msg_id == -2 ? " (outbox full)" : "");
        }
        return;
    }

    for (size_t k = 0; k < count; ++k) {
        subscriptions_[batchIndex[k]].sent = true;
    }
    batchMsgId_ = msg_id;
    batchSentTick_ = xTaskGetTickCount();
    ESP_LOGD(TAG, "Sent SUBSCRIBE batch of %u, msg_id %d", static_cast<unsigned>(count), msg_id);
}

void MqttBroker::updateConfig(const std::string& newUrl, uint32_t newPort, uint32_t newWsPort,
                             const std::string& newUsername, const std::string& newPassword,
                             const bool newAutoDiscovery) {
    ESP_LOGI(TAG, "Updating configuration for broker '%.*s'", (int)name.length(), name.data());

    url = newUrl;
    port = newPort;
    wsPort = newWsPort;
    username = newUsername;
    password = newPassword;
    autoDiscovery_ = newAutoDiscovery;

    ESP_LOGI(TAG, "Broker '%.*s' configured with URL: %s", (int)name.length(), name.data(), url.c_str());
}
