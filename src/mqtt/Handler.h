//
// Created by Bolke de Bruin on 10/08/2025.
//

#ifndef HANDLER_H
#define HANDLER_H
#include <esp_event_base.h>

void mqtt_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data);

#endif //HANDLER_H
