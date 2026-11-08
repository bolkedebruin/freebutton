/*
 * MQTT JavaScript Binding Abstraction Layer for JavaScript Bindings
 *
 * This header provides a C-compatible interface between the MQuickJS JavaScript
 * bindings (in src/scripting/js/freebutton_mqtt.c) and the C++ MQTT implementation
 * (in src/mqtt/).
 *
 * All functions use C linkage for compatibility with the pure C mquickjs engine.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

// Maximum lengths for MQTT data
#define MQTT_MAX_TOPIC_LEN 256
#define MQTT_MAX_PAYLOAD_LEN 512

/*
 * Publish an MQTT message
 *
 * Parameters:
 *   brokerId - Broker ID (0-based index)
 *   topic    - Topic string
 *   payload  - Message payload
 *   qos      - Quality of service (0, 1, or 2)
 *   retain   - Retain flag (0 or 1)
 *
 * Returns:
 *   0 on success, -1 on error
 */
int mqtt_binding_publish(uint8_t brokerId, const char* topic, const char* payload, int qos, int retain);

/*
 * Subscribe to an MQTT topic
 *
 * Parameters:
 *   brokerId - Broker ID (0-based index)
 *   topic    - Topic pattern (supports MQTT wildcards)
 *   qos      - Quality of service (0, 1, or 2)
 *
 * Returns:
 *   0 on success, -1 on error
 */
int mqtt_binding_subscribe(uint8_t brokerId, const char* topic, int qos);

/*
 * Unsubscribe from an MQTT topic
 *
 * Parameters:
 *   brokerId - Broker ID (0-based index)
 *   topic    - Topic pattern
 *
 * Returns:
 *   0 on success, -1 on error
 */
int mqtt_binding_unsubscribe(uint8_t brokerId, const char* topic);

/*
 * Check if broker is connected
 *
 * Parameters:
 *   brokerId - Broker ID (0-based index)
 *
 * Returns:
 *   1 if connected, 0 if not connected or invalid brokerId
 */
int mqtt_binding_is_connected(uint8_t brokerId);

/*
 * Get broker name
 *
 * Parameters:
 *   brokerId - Broker ID (0-based index)
 *
 * Returns:
 *   Broker name string, or NULL if invalid brokerId
 */
const char* mqtt_binding_get_broker_name(uint8_t brokerId);

/*
 * Get number of configured brokers
 *
 * Returns:
 *   Number of brokers
 */
int mqtt_binding_get_broker_count(void);

/*
 * JavaScript callback types
 *
 * Script code never runs in an MQTT task. The MQTT task copies each message
 * a JS subscription wants (and connect / disconnect events) into a bounded,
 * statically allocated queue and returns; the loop task delivers them to the
 * callbacks below with the JS engine lock held (MqttBinding::processPending).
 * When the queue is full the message is dropped and counted.
 */
// topic and payload point into the queue: they are NOT NUL-terminated and
// are valid only for the duration of the call.
typedef void (*mqtt_js_message_callback_t)(uint8_t brokerId, const char* topic, size_t topic_len,
                                           const char* payload, size_t length);
typedef void (*mqtt_js_connect_callback_t)(uint8_t brokerId);
typedef void (*mqtt_js_disconnect_callback_t)(uint8_t brokerId);
// Called from the MQTT task, with the JS MQTT lock held: nonzero when a JS
// subscription matches. Must only read the subscription table, nothing else.
typedef int (*mqtt_js_message_filter_t)(uint8_t brokerId, const char* topic, size_t topic_len);

/*
 * Register JavaScript callbacks for MQTT events
 */
void mqtt_binding_register_message_callback(mqtt_js_message_callback_t callback);
void mqtt_binding_register_connect_callback(mqtt_js_connect_callback_t callback);
void mqtt_binding_register_disconnect_callback(mqtt_js_disconnect_callback_t callback);
// Nothing is queued until a filter is registered.
void mqtt_binding_register_message_filter(mqtt_js_message_filter_t filter);

/*
 * The JS MQTT lock: guards the queue and every write of the fields the filter
 * reads (subscription topic / allocated). A leaf lock: held only for a table
 * update or one copy, never while calling script code or another lock.
 */
void mqtt_binding_lock(void);
void mqtt_binding_unlock(void);

#ifdef __cplusplus
}

/*
 * Called from MqttBroker event handlers (MQTT task). They only queue; they
 * never wait for script code (at most a few ms for the JS MQTT lock).
 */
namespace MqttBinding {
    // Zero-copy in: topic/payload are pointer + length, not NUL-terminated;
    // copied once, into the queue.
    void notifyMessageReceived(uint8_t brokerId, const char* topic, size_t topic_len,
                               const char* payload, size_t length);
    void notifyConnected(uint8_t brokerId);
    void notifyDisconnected(uint8_t brokerId);

    // Loop task: deliver queued events to JS (takes the JS engine lock).
    void processPending();
}
#endif
