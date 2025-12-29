/*
 * MQTT JavaScript Binding Abstraction Layer for JavaScript Bindings
 *
 * This header provides a C-compatible interface between the MQuickJS JavaScript
 * bindings (in lib/mquickjs/freebutton_mqtt.c) and the C++ MQTT implementation
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
 */
typedef void (*mqtt_js_message_callback_t)(uint8_t brokerId, const char* topic, const char* payload, size_t length);
typedef void (*mqtt_js_connect_callback_t)(uint8_t brokerId);
typedef void (*mqtt_js_disconnect_callback_t)(uint8_t brokerId);

/*
 * Register JavaScript callbacks for MQTT events
 */
void mqtt_binding_register_message_callback(mqtt_js_message_callback_t callback);
void mqtt_binding_register_connect_callback(mqtt_js_connect_callback_t callback);
void mqtt_binding_register_disconnect_callback(mqtt_js_disconnect_callback_t callback);

#ifdef __cplusplus
}

/*
 * C++ namespace for invoking JavaScript callbacks from MQTT broker
 * These functions are called from MqttBroker event handlers
 */
namespace MqttBinding {
    void notifyMessageReceived(uint8_t brokerId, const char* topic, const char* payload, size_t length);
    void notifyConnected(uint8_t brokerId);
    void notifyDisconnected(uint8_t brokerId);
}
#endif
