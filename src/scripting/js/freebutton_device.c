/*
 * FreeButton Device JavaScript Bindings
 *
 * Provides a proxy dispatcher (js_fb_device_proxy) used by the C-built
 * fb object tree. Each prototype method is a JS_NewCFunctionParams closure
 * that dispatches through this proxy to avoid any JS source parsing at init.
 *
 * Also provides topology query functions used internally by device_hardware.cpp.
 */

// IMPORTANT: Include mquickjs.h FIRST to establish proper type definitions
// mquickjs.h includes <inttypes.h> which defines stdint types needed by ESP-IDF headers
#include "mquickjs.h"
#include <stdio.h>
#include <stdlib.h>

// Import the hardware abstraction layer (ESP32 only)
#ifdef ESP_PLATFORM
#include "../device_hardware.h"
#include "../led_hardware.h"
#include "../button_hardware.h"
#else
// Host build stubs for generator
static inline int device_hw_get_connector_type(int connector) { return 0; }
static inline int device_hw_get_main_connector(void) { return -1; }
static inline int led_hw_set_color(int pos, uint8_t r, uint8_t g, uint8_t b) { return 0; }
static inline int button_hw_get_label(int position, char* out, size_t capacity) { if (capacity) out[0] = 0; return 0; }
static inline int button_hw_get_top_label(int position, char* out, size_t capacity) { if (capacity) out[0] = 0; return 0; }
#endif

// Method IDs for the proxy dispatcher (encoded in params of JS_NewCFunctionParams)
enum {
    FB_METHOD_LED_SET_COLOR = 0,
    FB_METHOD_LED_ON = 1,
    FB_METHOD_LED_OFF = 2,
    FB_METHOD_BTN_ON_CLICK = 3,
    FB_METHOD_BTN_ON_LONG_PRESS = 4,
    FB_METHOD_BTN_ON_RELEASE = 5,
    FB_METHOD_DISP_SET_LABEL = 6,
    FB_METHOD_DISP_SET_TOP_LABEL = 7,
    // Display item methods (forward argc/argv directly, no _p extraction)
    FB_METHOD_DITEM_CREATE = 8,
    FB_METHOD_DITEM_REMOVE = 9,
    FB_METHOD_DITEM_COUNT = 10,
    FB_METHOD_DITEM_GET = 11,
    FB_METHOD_DITEM_GET_ALL = 12,
    FB_METHOD_DITEM_SET_LABEL = 13,
    FB_METHOD_DITEM_SET_VALUE = 14,
    FB_METHOD_DITEM_SET_UNIT = 15,
    FB_METHOD_DITEM_SET_POSITION = 16,
    FB_METHOD_DITEM_SET_PAGE = 17,
    FB_METHOD_DISP_GET_LABEL = 18,
    FB_METHOD_DISP_GET_TOP_LABEL = 19,
    FB_METHOD_DITEM_SHOW_PAGE = 20,
    FB_METHOD_DITEM_GET_SHOWN_PAGE = 21,
};

/*
 * Topology query bindings (registered on __fb global via stdlib)
 */

JSValue js_freebutton_device_connectorType(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    int connector = 0;
    if (argc >= 1) {
        if (JS_ToInt32(ctx, &connector, argv[0]))
            return JS_EXCEPTION;
    } else {
        return JS_ThrowTypeError(ctx, "__fb.connectorType() requires connector argument");
    }
    return JS_NewInt32(ctx, device_hw_get_connector_type(connector));
}

JSValue js_freebutton_device_mainConnector(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    return JS_NewInt32(ctx, device_hw_get_main_connector());
}

/*
 * Proxy dispatcher for fb object tree methods.
 *
 * Each method on the fb hierarchy (e.g. fb.bars[0].left.led.setColor) is a
 * JS_NewCFunctionParams closure whose params encodes the method ID. The
 * instance's _p property provides the position/LED index. This avoids any
 * JS source parsing — methods dispatch directly to C hardware functions.
 *
 * Signature: generic_params (ctx, this_val, argc, argv, params)
 *
 * NOTE: This function references js_freebutton_button_* which are defined
 * in freebutton_button.c, included before this file in mquickjs_stdlib.c.
 * On the host build, this function is only referenced by name (stringified
 * by JS_CFUNC_SPECIAL_DEF) and never compiled.
 */
JSValue js_fb_device_proxy(JSContext *ctx, JSValue *this_val,
                           int argc, JSValue *argv, JSValue params)
{
    int method = JS_VALUE_GET_INT(params);

    /* Get position/LED index from this._p */
    JSValue p_val = JS_GetPropertyStr(ctx, *this_val, "_p");
    int pos;
    if (JS_ToInt32(ctx, &pos, p_val))
        return JS_EXCEPTION;

    switch (method) {
    case FB_METHOD_LED_SET_COLOR: {
        if (argc < 3)
            return JS_ThrowTypeError(ctx, "setColor requires r, g, b");
        int r, g, b;
        if (JS_ToInt32(ctx, &r, argv[0])) return JS_EXCEPTION;
        if (JS_ToInt32(ctx, &g, argv[1])) return JS_EXCEPTION;
        if (JS_ToInt32(ctx, &b, argv[2])) return JS_EXCEPTION;
        r = r < 0 ? 0 : r > 255 ? 255 : r;
        g = g < 0 ? 0 : g > 255 ? 255 : g;
        b = b < 0 ? 0 : b > 255 ? 255 : b;
        led_hw_set_color(pos, (uint8_t)r, (uint8_t)g, (uint8_t)b);
        return JS_UNDEFINED;
    }
    case FB_METHOD_LED_ON:
        led_hw_set_color(pos, 255, 255, 255);
        return JS_UNDEFINED;
    case FB_METHOD_LED_OFF:
        led_hw_set_color(pos, 0, 0, 0);
        return JS_UNDEFINED;
    case FB_METHOD_BTN_ON_CLICK: {
        if (argc < 1) return JS_ThrowTypeError(ctx, "onClick requires callback");
        JSValue tmp[2];
        tmp[0] = JS_NewInt32(ctx, pos);
        tmp[1] = argv[0];
        return js_freebutton_button_onClick(ctx, this_val, 2, tmp);
    }
    case FB_METHOD_BTN_ON_LONG_PRESS: {
        if (argc < 1) return JS_ThrowTypeError(ctx, "onLongPress requires callback");
        JSValue tmp[2];
        tmp[0] = JS_NewInt32(ctx, pos);
        tmp[1] = argv[0];
        return js_freebutton_button_onLongPress(ctx, this_val, 2, tmp);
    }
    case FB_METHOD_BTN_ON_RELEASE: {
        if (argc < 1) return JS_ThrowTypeError(ctx, "onRelease requires callback");
        JSValue tmp[2];
        tmp[0] = JS_NewInt32(ctx, pos);
        tmp[1] = argv[0];
        return js_freebutton_button_onRelease(ctx, this_val, 2, tmp);
    }
    case FB_METHOD_DISP_SET_LABEL: {
        if (argc < 1) return JS_ThrowTypeError(ctx, "setLabel requires text");
        JSValue tmp[2];
        tmp[0] = JS_NewInt32(ctx, pos);
        tmp[1] = argv[0];
        return js_freebutton_button_setLabel(ctx, this_val, 2, tmp);
    }
    case FB_METHOD_DISP_SET_TOP_LABEL: {
        if (argc < 1) return JS_ThrowTypeError(ctx, "setTopLabel requires text");
        JSValue tmp[2];
        tmp[0] = JS_NewInt32(ctx, pos);
        tmp[1] = argv[0];
        return js_freebutton_button_setTopLabel(ctx, this_val, 2, tmp);
    }
    case FB_METHOD_DISP_GET_LABEL:
    case FB_METHOD_DISP_GET_TOP_LABEL: {
        char text[32];  /* >= a button label slot's text (26 B + NUL) */
        if (method == FB_METHOD_DISP_GET_LABEL)
            button_hw_get_label(pos, text, sizeof(text));
        else
            button_hw_get_top_label(pos, text, sizeof(text));
        return JS_NewString(ctx, text);
    }
    /* Display item methods - forward argc/argv directly (explicit ID args, no _p) */
    case FB_METHOD_DITEM_CREATE:
        return js_freebutton_display_create(ctx, this_val, argc, argv);
    case FB_METHOD_DITEM_REMOVE:
        return js_freebutton_display_remove(ctx, this_val, argc, argv);
    case FB_METHOD_DITEM_COUNT:
        return js_freebutton_display_count(ctx, this_val, argc, argv);
    case FB_METHOD_DITEM_GET:
        return js_freebutton_display_get(ctx, this_val, argc, argv);
    case FB_METHOD_DITEM_GET_ALL:
        return js_freebutton_display_getAll(ctx, this_val, argc, argv);
    case FB_METHOD_DITEM_SET_LABEL:
        return js_freebutton_display_setLabel(ctx, this_val, argc, argv);
    case FB_METHOD_DITEM_SET_VALUE:
        return js_freebutton_display_setValue(ctx, this_val, argc, argv);
    case FB_METHOD_DITEM_SET_UNIT:
        return js_freebutton_display_setUnit(ctx, this_val, argc, argv);
    case FB_METHOD_DITEM_SET_POSITION:
        return js_freebutton_display_setPosition(ctx, this_val, argc, argv);
    case FB_METHOD_DITEM_SET_PAGE:
        return js_freebutton_display_setPage(ctx, this_val, argc, argv);
    case FB_METHOD_DITEM_SHOW_PAGE:
        return js_freebutton_display_showPage(ctx, this_val, argc, argv);
    case FB_METHOD_DITEM_GET_SHOWN_PAGE:
        return js_freebutton_display_getShownPage(ctx, this_val, argc, argv);
    default:
        return JS_ThrowInternalError(ctx, "unknown fb method %d", method);
    }
}
