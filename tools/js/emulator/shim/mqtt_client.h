// Emulator shim: the esp-mqtt names src/mqtt/Broker.h mentions. There is no
// client: tools/js/emulator/seam.cpp defines the MqttBroker members the
// script binding uses, on top of the host's transport.
#pragma once
#include <stdint.h>
typedef struct esp_mqtt_client* esp_mqtt_client_handle_t;
typedef struct esp_mqtt_error_codes esp_mqtt_error_codes;
typedef struct esp_mqtt_event_t* esp_mqtt_event_handle_t;
typedef const char* esp_event_base_t;
static inline int esp_mqtt_client_get_outbox_size(esp_mqtt_client_handle_t client) { (void)client; return 0; }
