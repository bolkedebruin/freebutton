//
// Created by Bolke de Bruin on 06/08/2025.
//

#ifndef WIFIMANAGER_H
#define WIFIMANAGER_H

void start_wifi_manager();
void reset_wifi_credentials();
void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
void setup_wifi_event_handler();

bool is_wifi_connected();
bool is_portal_started();
// True while the WiFi configuration portal (AP + captive DNS) is running.
bool is_captive_portal_active();

#endif //WIFIMANAGER_H
