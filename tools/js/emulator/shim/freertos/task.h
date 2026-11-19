// Emulator shim: see FreeRTOS.h. A created task never runs.
#pragma once
#include "FreeRTOS.h"
#ifdef __cplusplus
extern "C" {
#endif
// The loop task, or the MQTT task while the client delivers an event
// (tools/js/emulator/mqtt_client_shim.cpp): the firmware tells them apart.
TaskHandle_t xTaskGetCurrentTaskHandle(void);
#ifdef __cplusplus
}
#endif
static inline BaseType_t xTaskCreate(TaskFunction_t fn, const char* name, uint32_t stack, void* arg, UBaseType_t prio,
                                     TaskHandle_t* handle) {
    (void)fn; (void)name; (void)stack; (void)arg; (void)prio;
    if (handle) *handle = xTaskGetCurrentTaskHandle();
    return pdPASS;
}
static inline void vTaskDelete(TaskHandle_t handle) { (void)handle; }
static inline void vTaskDelay(TickType_t ticks) { (void)ticks; }
