/*
 * Reset of the JavaScript binding tables for an engine restart
 * (new bytecode without a reboot).
 *
 * The bindings keep, outside the JS heap, what a script registered: timers,
 * key handlers, MQTT subscriptions and connect / disconnect handlers, sensor
 * onChange handlers. Each entry holds a JSGCRef into the context that
 * registered it. Before that context is freed, all of it must go.
 *
 * Call sequence for a restart, with the engine lock held, on the task that
 * runs the engine, and NOT from inside script code (not from a native
 * function a script called, not from a timer / MQTT / key / sensor handler:
 * those still use the context after they return):
 *
 *   js_bindings_reset(ctx);     // forget every registration
 *   JS_FreeContext(ctx);        // old context gone
 *   JS_NewContext(...);         // same heap buffer
 *   ...load and run the new bytecode
 *
 * JSGCRef: the refs are NOT removed with JS_DeleteGCRef(). The list they are
 * linked into (ctx->last_gc_ref) lives in the context, JS_FreeContext() does
 * not walk it, and JS_NewContext() starts with an empty list; so the structs
 * are only cleared (js_gcref_forget) together with their "allocated" flag.
 * JS_DeleteGCRef() would abort() on a ref that is not in the list, clearing
 * cannot fail. The flag is what matters afterwards: with it cleared, the next
 * registration calls JS_AddGCRef() on the new context (which overwrites
 * `prev`) and never JS_DeleteGCRef() for a ref of the old one.
 * Consequence: between the reset and JS_FreeContext() the old context's ref
 * list is cut short, so the context must not run script code or a garbage
 * collection that anything still depends on. These functions are for a
 * context that is about to be freed, not for "stop the script, keep the
 * context".
 *
 * Every function is safe when nothing was registered and when called twice.
 * None allocates. `ctx` is the context about to be freed; it is not used and
 * may be NULL.
 */

#ifndef JS_RESET_H
#define JS_RESET_H

#ifdef __cplusplus
extern "C" {
#endif

#include "mquickjs.h"

// Clear a JSGCRef that was linked with JS_AddGCRef() into a context that is
// about to be freed (see above). Not a replacement for JS_DeleteGCRef() on a
// context that stays in use.
static inline void js_gcref_forget(JSGCRef *ref)
{
    ref->val = JS_UNDEFINED;
    ref->prev = NULL;
}

// Forget everything the running script registered. Called with the
// engine lock held, on the task that runs the engine, right BEFORE
// JS_FreeContext(): the context still exists but must not be used to run
// script code. After the call no firmware task may call into JS for an
// old registration, and every table is as it is after boot.

// Timers and intervals (stdlib_export.c). Ids keep counting up.
void js_timers_reset(JSContext *ctx);
// Key click / long press / release handlers (freebutton_button.c), and the
// native registrations that make the button task call into JS.
void js_button_callbacks_reset(JSContext *ctx);
// MQTT subscriptions (also at the brokers), connect / disconnect handlers and
// the queued events (freebutton_mqtt.c, mqtt_binding.cpp).
void js_mqtt_reset(JSContext *ctx);
// Sensor onChange handlers (freebutton_sensor.c, sensor_binding.cpp).
void js_sensor_callbacks_reset(JSContext *ctx);
// Calls the four above. Firmware only (stdlib_export.c).
void js_bindings_reset(JSContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // JS_RESET_H
