//
// Created by Bolke de Bruin on 10/08/2025.
//
#include <esp_event_base.h>
#include "mqtt_client.h"

#include "esp_log.h"

static const auto TAG = "mqtt";

void mqtt_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data) {
    const auto event = static_cast<esp_mqtt_event_handle_t>(event_data);
    //auto it = clientToBroker.find(event->client);
    //if (it == clientToBroker.end()) return;
    //auto* broker = it->second;

    switch (static_cast<esp_mqtt_event_id_t>(event_id)) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Connected to MQTT");
            //broker->connected();
            //broker->sendBirth();
            //broker->sendDiscovery();
            break;
        case MQTT_EVENT_DISCONNECTED:
            break;
        case MQTT_EVENT_SUBSCRIBED:
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            break;
        case MQTT_EVENT_PUBLISHED:
            break;
        case MQTT_EVENT_DATA:
            //broker->handleMessage(event->topic, event->topic_len, event->data, event->data_len);
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT_EVENT_ERROR");
            break;
        default:
            break;
    }
}