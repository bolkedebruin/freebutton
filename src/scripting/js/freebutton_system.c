/*
 * FreeButton System JavaScript Bindings
 *
 * Provides JavaScript API for system information:
 *   system.uptime()    - Get uptime in seconds
 *   system.freeHeap()  - Get free heap memory in bytes
 *   system.version()   - Get firmware version string
 */

#include <stdio.h>
#include <stdlib.h>
#include "mquickjs.h"

#ifdef ESP_PLATFORM
#include "../system_binding.h"
#else
// Host build stubs for generator
static inline uint32_t system_hw_get_uptime(void) { return 0; }
static inline uint32_t system_hw_get_free_heap(void) { return 0; }
static inline const char* system_hw_get_version(void) { return ""; }
#endif

/**
 * @jsapi system.uptime
 * @description Get device uptime in seconds
 * @returns {number} Uptime in seconds
 */
JSValue js_freebutton_system_uptime(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    uint32_t uptime = system_hw_get_uptime();
    return JS_NewInt32(ctx, (int32_t)uptime);
}

/**
 * @jsapi system.freeHeap
 * @description Get free heap memory in bytes
 * @returns {number} Free heap size in bytes
 */
JSValue js_freebutton_system_freeHeap(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    uint32_t freeHeap = system_hw_get_free_heap();
    return JS_NewInt32(ctx, (int32_t)freeHeap);
}

/**
 * @jsapi system.version
 * @description Get firmware version string
 * @returns {string} Firmware version
 */
JSValue js_freebutton_system_version(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    const char* version = system_hw_get_version();
    return JS_NewString(ctx, version);
}
