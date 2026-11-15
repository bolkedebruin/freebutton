// Emulator shim: one thread, so every lock is free and every take succeeds.
#pragma once
#include <stdint.h>
typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef void* SemaphoreHandle_t;
typedef void* TaskHandle_t;
typedef struct { int unused; } StaticSemaphore_t;
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define portMAX_DELAY 0xffffffffu
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#ifdef __cplusplus
extern "C" {
#endif
// Milliseconds of the emulator's clock (tools/js/emulator/runner.c).
TickType_t xTaskGetTickCount(void);
#ifdef __cplusplus
}
#endif
