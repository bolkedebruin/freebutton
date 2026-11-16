// Emulator shim: see FreeRTOS.h.
#pragma once
#include "FreeRTOS.h"
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) { static int one; return &one; }
static inline SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t* buffer) { return buffer; }
static inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutexStatic(StaticSemaphore_t* buffer) { return buffer; }
static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t handle, TickType_t wait) { (void)handle; (void)wait; return pdTRUE; }
static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t handle) { (void)handle; return pdTRUE; }
static inline BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t handle, TickType_t wait) { (void)handle; (void)wait; return pdTRUE; }
static inline BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t handle) { (void)handle; return pdTRUE; }
