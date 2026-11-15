// Export js_stdlib from the generated header
// Provide functions for REPL/embedded use (from mqjs.c)

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "mquickjs.h"
#include "mquickjs_ext.h"
#include "js_reset.h"

// Console output callback for WebSocket REPL
// When set, console output is sent to both stdout AND the callback (see mquickjs.h)
static JSConsoleWriteFunc g_console_write_func = NULL;

void JS_SetConsoleWriteFunc(JSConsoleWriteFunc func) {
    g_console_write_func = func;
}

#ifdef EMSCRIPTEN
// For WASM builds, include stub declarations
#include "freebutton_stubs_wasm.h"
#else
// Import the file system hardware abstraction layer
#include "../file_hardware.h"
#endif

// Helper to get current time in milliseconds
static int64_t get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + (tv.tv_usec / 1000);
}

// GC function (from mqjs.c)
JSValue js_gc(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv) {
    (void)this_val;
    (void)argc;
    (void)argv;
    JS_GC(ctx);
    return JS_UNDEFINED;
}

// Load and evaluate a JavaScript file from LittleFS
// Supports both source (.js) and pre-compiled bytecode (.jsc)
JSValue js_load(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv) {
    (void)this_val;
#ifdef EMSCRIPTEN
    // Not available in WASM builds (only used at runtime, not during compilation)
    (void)argc;
    (void)argv;
    return JS_ThrowError(ctx, JS_CLASS_ERROR, "load() not available in browser");
#else
    const char *filename;
    JSCStringBuf buf_str;
    uint8_t *buf;
    int buf_len;
    JSValue ret;

    filename = JS_ToCString(ctx, argv[0], &buf_str);
    if (!filename)
        return JS_EXCEPTION;

    // Load file from LittleFS (with default size limits)
    buf = file_hw_load(filename, &buf_len, 0);
    if (!buf) {
        return JS_ThrowError(ctx, JS_CLASS_ERROR, "failed to load file '%s'", filename);
    }

    // Check if it's pre-compiled bytecode
    if (JS_IsBytecode(buf, buf_len)) {
        // Bytecode: relocate and load (zero-copy after relocation)
        if (JS_RelocateBytecode(ctx, buf, buf_len) != 0) {
            free(buf);
            return JS_ThrowError(ctx, JS_CLASS_ERROR, "failed to relocate bytecode '%s'", filename);
        }
        ret = JS_LoadBytecode(ctx, buf);
        // Note: buf must remain allocated as long as the bytecode is used
        // In a production system, you'd track this and free it when the context is destroyed
        // For now, this is a memory leak but bytecode is typically loaded once at startup
    } else {
        // Source code: parse and evaluate (requires full source in memory)
        ret = JS_Eval(ctx, (const char *)buf, buf_len, filename, 0);
        // Source is copied during parsing, safe to free
        free(buf);
    }

    return ret;
#endif
}

// Timer support (from mqjs.c)
typedef struct {
    bool allocated;
    JSGCRef func;
    int64_t timeout; /* in ms */
    int32_t id;      /* what setTimeout returned; unique, never 0 */
} JSTimer;

#define MAX_TIMERS 16
static JSTimer js_timer_list[MAX_TIMERS];
/* Timer ids are unique and start at 1, as in browsers: scripts commonly
 * test `if (timer)`, and clearTimeout() of a timer that already fired must
 * not cancel a later timer that reuses the same slot. */
static int32_t js_timer_next_id = 1;

JSValue js_setTimeout(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv) {
    (void)this_val;
    JSTimer *th;
    int delay, i;
    JSValue *pfunc;

    if (!JS_IsFunction(ctx, argv[0]))
        return JS_ThrowTypeError(ctx, "not a function");
    if (JS_ToInt32(ctx, &delay, argv[1]))
        return JS_EXCEPTION;
    for(i = 0; i < MAX_TIMERS; i++) {
        th = &js_timer_list[i];
        if (!th->allocated) {
            pfunc = JS_AddGCRef(ctx, &th->func);
            *pfunc = argv[0];
            th->timeout = get_time_ms() + delay;
            th->allocated = true;
            th->id = js_timer_next_id;
            js_timer_next_id = (js_timer_next_id == INT32_MAX) ? 1 : js_timer_next_id + 1;
            return JS_NewInt32(ctx, th->id);
        }
    }
    return JS_ThrowInternalError(ctx, "too many timers");
}

JSValue js_clearTimeout(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv) {
    (void)this_val;
    int timer_id;
    JSTimer *th;

    if (JS_ToInt32(ctx, &timer_id, argv[0]))
        return JS_EXCEPTION;
    if (timer_id <= 0)
        return JS_UNDEFINED;  /* never a valid id (e.g. a "no timer" 0) */
    for (int i = 0; i < MAX_TIMERS; i++) {
        th = &js_timer_list[i];
        if (th->allocated && th->id == timer_id) {
            JS_DeleteGCRef(ctx, &th->func);
            th->allocated = false;
            break;
        }
    }
    return JS_UNDEFINED;
}

JSValue js_date_now(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv) {
    (void)this_val;
    (void)argc;
    (void)argv;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return JS_NewInt64(ctx, (int64_t)tv.tv_sec * 1000 + (tv.tv_usec / 1000));
}

// Date constructor for the firmware embedder (mirrors the REPL's mqjs.c).
// The Date class registered in mqjs_stdlib.c references this symbol.
JSValue js_date_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv) {
    (void)this_val;
    double val;
    argc &= ~FRAME_CF_CTOR;
    if (argc == 0) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        val = (double)((int64_t)tv.tv_sec * 1000 + (tv.tv_usec / 1000));
    } else if (argc == 1 && JS_IsNumber(ctx, argv[0])) {
        if (JS_ToNumber(ctx, &val, argv[0]))
            return JS_EXCEPTION;
    } else {
        return JS_ThrowTypeError(ctx, "unsupported Date() parameter");
    }
    return JS_NewDate(ctx, val);
}

JSValue js_print(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv) {
    // Print all arguments to both stdout AND WebSocket (if callback is set)
    // Zero-buffering: each piece streamed immediately to both destinations
    (void)this_val;

    for (int i = 0; i < argc; i++) {
        if (i > 0) {
            putchar(' ');                                      // Always to stdout
            if (g_console_write_func) g_console_write_func(" ", 1);  // Also to WebSocket
        }

        // Convert value to string and output
        JSCStringBuf buf;
        size_t len;
        const char *str = JS_ToCStringLen(ctx, &len, argv[i], &buf);
        if (str) {
            fwrite(str, 1, len, stdout);                       // Always to stdout
            if (g_console_write_func) g_console_write_func(str, len);  // Also to WebSocket
        }
    }

    // Add newline to both outputs
    putchar('\n');
    if (g_console_write_func) g_console_write_func("\n", 1);

    return JS_UNDEFINED;
}

JSValue js_performance_now(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv) {
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewInt64(ctx, get_time_ms());
}

// Engine restart: drop every pending timer and interval (js_reset.h). The
// refs are cleared, not deleted: the context is about to be freed.
// js_timer_next_id keeps counting, so an id the old script got never names a
// timer of the new script.
void js_timers_reset(JSContext *ctx) {
    (void)ctx;
    for (int i = 0; i < MAX_TIMERS; i++) {
        JSTimer *th = &js_timer_list[i];
        th->allocated = false;
        js_gcref_forget(&th->func);
        th->timeout = 0;
        th->id = 0;
    }
}

#ifndef EMSCRIPTEN
// Engine restart: forget everything the script registered (js_reset.h).
// MQTT first: it stops the MQTT tasks queueing events for the old script.
void js_bindings_reset(JSContext *ctx) {
    js_mqtt_reset(ctx);
    js_timers_reset(ctx);
    js_button_callbacks_reset(ctx);
    js_sensor_callbacks_reset(ctx);
}
#endif

// Process timers - call this periodically from your main loop
// Returns the number of milliseconds until the next timer needs to run
int64_t JS_ProcessTimers(JSContext *ctx) {
    int64_t min_delay = 1000;
    int64_t cur_time = get_time_ms();
    bool has_timer = false;

    for(int i = 0; i < MAX_TIMERS; i++) {
        JSTimer *th = &js_timer_list[i];
        if (th->allocated) {
            has_timer = true;
            int64_t delay = th->timeout - cur_time;
            if (delay <= 0) {
                // Timer expired - execute it (following mqjs.c pattern)
                JSValue ret;
                JS_PushArg(ctx, th->func.val); /* func */
                JS_PushArg(ctx, JS_NULL); /* this */

                // Clean up timer before calling (in case timer re-schedules itself)
                JS_DeleteGCRef(ctx, &th->func);
                th->allocated = false;

                ret = JS_Call(ctx, 0);
                if (JS_IsException(ret)) {
                    // Log exception but don't exit (unlike mqjs.c which exits)
                    JSValue exception = JS_GetException(ctx);
                    (void)exception; // Could log this
                }
                min_delay = 0;
                break;
            } else if (delay < min_delay) {
                min_delay = delay;
            }
        }
    }

    return has_timer ? min_delay : -1;
}

// Forward declarations for LED functions (defined in freebutton_led.c)
JSValue js_freebutton_led_count(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_freebutton_led_on(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_freebutton_led_off(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_freebutton_led_setColor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);

// Forward declarations for display functions (defined in freebutton_displayitem.c)
JSValue js_freebutton_display_create(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_freebutton_display_remove(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_freebutton_display_count(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_freebutton_display_get(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_freebutton_display_getAll(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_freebutton_display_setLabel(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_freebutton_display_setValue(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_freebutton_display_setUnit(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_freebutton_display_setPosition(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_freebutton_display_setPage(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);

// Include the generated FreeButton stdlib (with LED bindings)
#include "freebutton_stdlib.h"
