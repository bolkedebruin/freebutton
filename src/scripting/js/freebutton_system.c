/*
 * FreeButton System JavaScript Bindings
 *
 * Provides JavaScript API for system information:
 *   system.uptime()    - Get uptime in seconds
 *   system.freeHeap()  - Get free heap memory in bytes
 *   system.version()   - Get firmware version string
 *   system.id()        - Get the device id
 *   system.name()      - Get the device name
 *   system.model()     - Get the hardware model
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
static inline const char* system_hw_get_id(void) { return ""; }
static inline const char* system_hw_get_model(void) { return ""; }
static inline size_t system_hw_get_name(char* buf, size_t cap) { (void)buf; (void)cap; return 0; }
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

/**
 * @jsapi system.id
 * @description Get the device id, for example "btn_a00001": the first part of the device's MQTT topics and its network name. Each call makes a new string: call it once and keep the result.
 * @returns {string} Device id
 */
JSValue js_freebutton_system_id(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    return JS_NewString(ctx, system_hw_get_id());
}

/**
 * @jsapi system.name
 * @description Get the device name: "core.name" of config.json, or the device id when the config has none. Each call makes a new string.
 * @returns {string} Device name
 */
JSValue js_freebutton_system_name(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    char name[64];
    const size_t len = system_hw_get_name(name, sizeof(name));
    return JS_NewStringLen(ctx, name, len);
}

/**
 * @jsapi system.model
 * @description Get the hardware model, for example "Model 1 Version 9". Each call makes a new string.
 * @returns {string} Hardware model
 */
JSValue js_freebutton_system_model(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    return JS_NewString(ctx, system_hw_get_model());
}
