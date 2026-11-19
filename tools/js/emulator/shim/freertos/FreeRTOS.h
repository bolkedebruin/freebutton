// Emulator shim: one thread, so every lock is free and every take succeeds.
// Tasks the firmware starts do not run by themselves: the runner calls what
// they would do (tools/js/emulator/device.cpp).
#pragma once
#include "esp_log.h"
#include <stdint.h>
typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef void* SemaphoreHandle_t;
typedef void* TaskHandle_t;
typedef void (*TaskFunction_t)(void*);
typedef struct { int unused; } StaticSemaphore_t;
typedef struct { int unused; } portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED {0}
#define portENTER_CRITICAL(mux) ((void)(mux))
#define portEXIT_CRITICAL(mux) ((void)(mux))
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define portMAX_DELAY 0xffffffffu
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define pdTICKS_TO_MS(ticks) ((uint32_t)(ticks))
#ifdef __cplusplus
extern "C" {
#endif
// Milliseconds of the emulator's clock (tools/js/emulator/runner.c).
TickType_t xTaskGetTickCount(void);
#ifdef __cplusplus
}
#endif
