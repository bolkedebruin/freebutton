// Emulator shim: the esp-mqtt client API that src/mqtt/Broker.cpp uses. The
// client is tools/js/emulator/mqtt_client_shim.cpp: it hands SUBSCRIBE /
// UNSUBSCRIBE / PUBLISH to the host's transport and turns what the host
// reports (connected, disconnected, a message) into esp-mqtt events.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL (-1)

typedef const char* esp_event_base_t;
typedef void (*esp_event_handler_t)(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data);

typedef enum {
    MQTT_EVENT_ANY = -1,
    MQTT_EVENT_ERROR = 0,
    MQTT_EVENT_CONNECTED,
    MQTT_EVENT_DISCONNECTED,
    MQTT_EVENT_SUBSCRIBED,
    MQTT_EVENT_UNSUBSCRIBED,
    MQTT_EVENT_PUBLISHED,
    MQTT_EVENT_DATA,
    MQTT_EVENT_BEFORE_CONNECT,
    MQTT_EVENT_DELETED,
    MQTT_USER_EVENT,
} esp_mqtt_event_id_t;

typedef enum {
    MQTT_ERROR_TYPE_NONE = 0,
    MQTT_ERROR_TYPE_TCP_TRANSPORT,
    MQTT_ERROR_TYPE_CONNECTION_REFUSED,
    MQTT_ERROR_TYPE_SUBSCRIBE_FAILED,
} esp_mqtt_error_type_t;

typedef struct esp_mqtt_error_codes {
    esp_mqtt_error_type_t error_type;
} esp_mqtt_error_codes_t;

typedef struct esp_mqtt_client* esp_mqtt_client_handle_t;

typedef struct esp_mqtt_event_t {
    esp_mqtt_event_id_t event_id;
    esp_mqtt_client_handle_t client;
    char* data;
    int data_len;
    int total_data_len;
    int current_data_offset;
    char* topic;
    int topic_len;
    int msg_id;
    esp_mqtt_error_codes_t* error_handle;
} esp_mqtt_event_t;
typedef esp_mqtt_event_t* esp_mqtt_event_handle_t;

typedef struct {
    const char* filter;
    int qos;
} esp_mqtt_topic_t;

typedef struct esp_mqtt_client_config_t {
    struct {
        struct {
            const char* uri;
            uint32_t port;
        } address;
    } broker;
    struct {
        const char* username;
        const char* client_id;
        struct {
            const char* password;
        } authentication;
    } credentials;
    struct {
        struct {
            const char* topic;
            const char* msg;
            int qos;
            int retain;
        } last_will;
        int keepalive;
        bool disable_clean_session;
    } session;
    struct {
        int reconnect_timeout_ms;
        int timeout_ms;
        bool disable_auto_reconnect;
    } network;
    struct {
        int priority;
        int stack_size;
    } task;
    struct {
        int size;
        int out_size;
    } buffer;
    struct {
        uint64_t limit;
    } outbox;
} esp_mqtt_client_config_t;

esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t* config);
esp_err_t esp_mqtt_client_register_event(esp_mqtt_client_handle_t client, esp_mqtt_event_id_t event,
                                         esp_event_handler_t handler, void* handler_args);
esp_err_t esp_mqtt_client_start(esp_mqtt_client_handle_t client);
esp_err_t esp_mqtt_client_stop(esp_mqtt_client_handle_t client);
esp_err_t esp_mqtt_client_destroy(esp_mqtt_client_handle_t client);
int esp_mqtt_client_publish(esp_mqtt_client_handle_t client, const char* topic, const char* data, int len, int qos,
                            int retain);
int esp_mqtt_client_subscribe_multiple(esp_mqtt_client_handle_t client, const esp_mqtt_topic_t* topics, int count);
int esp_mqtt_client_unsubscribe(esp_mqtt_client_handle_t client, const char* topic);
int esp_mqtt_client_get_outbox_size(esp_mqtt_client_handle_t client);
esp_err_t esp_mqtt_dispatch_custom_event(esp_mqtt_client_handle_t client, esp_mqtt_event_t* event);

#ifdef __cplusplus
}
#endif
