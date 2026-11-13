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
#include "font_arg.h"

// Import the hardware abstraction layer (ESP32 only)
#ifdef ESP_PLATFORM
#include "../device_hardware.h"
#include "../led_hardware.h"
#include "../button_hardware.h"
#include "../list_binding.h"
#include "../image_binding.h"
#else
// Host build stubs for generator
static inline int device_hw_get_connector_type(int connector) { return 0; }
static inline int device_hw_get_main_connector(void) { return -1; }
static inline int led_hw_set_color(int pos, uint8_t r, uint8_t g, uint8_t b) { return 0; }
static inline int button_hw_get_label(int position, char* out, size_t capacity) { if (capacity) out[0] = 0; return 0; }
static inline int button_hw_get_top_label(int position, char* out, size_t capacity) { if (capacity) out[0] = 0; return 0; }
static inline bool button_hw_find_by_name(const char* name, size_t len, int* position) { return false; }
static inline int list_hw_find(const char* name, size_t len) { return -1; }
static inline int list_hw_count(int index) { return -1; }
static inline int list_hw_get_selected(int index) { return -1; }
static inline int list_hw_move(int index, int step) { return -1; }
static inline bool list_hw_select(int index, int item) { return false; }
static inline void* list_hw_items_new(void) { return NULL; }
static inline bool list_hw_items_add(void* b, const char* text, size_t len) { return false; }
static inline bool list_hw_items_commit(int index, void* b) { return false; }
static inline void list_hw_items_free(void* b) {}
static inline int image_hw_find(const char* name, size_t len) { return -1; }
static inline bool image_hw_exists(int index) { return false; }
static inline int image_hw_set_icon(int index, const char* name, size_t len) { return -1; }
static inline int image_hw_set_src(int index, const char* key, size_t len) { return -1; }
static inline int button_hw_set_font(int position, int face, int size, bool top) { return -1; }
static inline int button_hw_restore_font(int position, bool top) { return -1; }
static inline bool list_hw_set_font(int index, int face, int size) { return false; }
static inline bool list_hw_restore_font(int index) { return false; }
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
    // fb.key(name): the key object of a config.json key name
    FB_METHOD_KEY = 22,
    // fb.main.list.*(nameOrIndex, ...): main-display lists (config "lists")
    FB_METHOD_LIST_NEXT = 23,
    FB_METHOD_LIST_PREV = 24,
    FB_METHOD_LIST_SELECT = 25,
    FB_METHOD_LIST_GET_SELECTED = 26,
    FB_METHOD_LIST_COUNT = 27,
    FB_METHOD_LIST_SET_ITEMS = 28,
    // key display .setIcon(name) (_p = key position)
    FB_METHOD_DISP_SET_ICON = 29,
    // fb.main.display.setIcon(id, name)
    FB_METHOD_DITEM_SET_ICON = 30,
    // fb.main.image.*(nameOrIndex, value): main-display images (config "images")
    FB_METHOD_IMAGE_SET_ICON = 31,
    FB_METHOD_IMAGE_SET_SRC = 32,
    // key display .setFont(font, size) / .setTopFont(font, size) (_p = key position)
    FB_METHOD_DISP_SET_FONT = 33,
    FB_METHOD_DISP_SET_TOP_FONT = 34,
    // fb.main.display.setFont(id, font, size)
    FB_METHOD_DITEM_SET_FONT = 35,
    // fb.main.list.setFont(nameOrIndex, font, size)
    FB_METHOD_LIST_SET_FONT = 36,
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
 * fb.key(name) result: the existing key object for a 1-based key position,
 * the same object as fb.main.left / .right or fb.bars[i].left / .right
 * (device_hw_init_fb builds them: position = connector * 2 + 1 (left) or
 * + 2 (right); bars are numbered by the BAR connectors in connector order).
 * No object is created: the tree is walked by property reads (each read
 * makes a short-lived property-name string, garbage right after; call
 * fb.key() once at setup and keep the result in a local var where it is
 * used often). The value is kept in a GC ref across the reads (the GC
 * moves objects).
 */
static JSValue fb_key_object(JSContext *ctx, int position)
{
    const int conn = (position - 1) / 2;
    const char *side = ((position - 1) % 2) ? "right" : "left";
    const int type = position >= 1 ? device_hw_get_connector_type(conn) : 0;
    const int isMain = type == 2 && conn == device_hw_get_main_connector();
    if (!isMain && type != 1)
        return JS_ThrowReferenceError(ctx, "key at position %d is not connected", position);

    JSGCRef ref;
    JSValue *v = JS_PushGCRef(ctx, &ref);
    *v = JS_GetGlobalObject(ctx);
    *v = JS_GetPropertyStr(ctx, *v, "fb");
    if (isMain) {
        *v = JS_GetPropertyStr(ctx, *v, "main");
    } else {
        uint32_t bar = 0;
        for (int c = 0; c < conn; c++) {
            if (device_hw_get_connector_type(c) == 1)
                bar++;
        }
        *v = JS_GetPropertyStr(ctx, *v, "bars");
        *v = JS_GetPropertyUint32(ctx, *v, bar);
    }
    *v = JS_GetPropertyStr(ctx, *v, side);
    return JS_PopGCRef(ctx, &ref);
}

/*
 * First argument of fb.main.list.*: a list's config name (looked up in the
 * firmware's name table, not copied into the JS heap) or its index in
 * config.json "lists". Unknown name: ReferenceError; no list at the index:
 * RangeError.
 */
static int fb_list_arg(JSContext *ctx, int argc, JSValue *argv, int *index)
{
    if (argc < 1) {
        JS_ThrowTypeError(ctx, "fb.main.list: a list name is required");
        return -1;
    }
    if (JS_IsString(ctx, argv[0])) {
        JSCStringBuf buf;
        size_t len;
        const char *name = JS_ToCStringLen(ctx, &len, argv[0], &buf);
        if (!name)
            return -1;
        *index = list_hw_find(name, len);
        if (*index < 0) {
            JS_ThrowReferenceError(ctx, "unknown list \"%.32s\"", name);
            return -1;
        }
        return 0;
    }
    if (JS_ToInt32(ctx, index, argv[0]))
        return -1;
    if (list_hw_count(*index) < 0) {
        JS_ThrowRangeError(ctx, "no list %d", *index);
        return -1;
    }
    return 0;
}

/*
 * First argument of fb.main.image.*: an image's config name (looked up in
 * the firmware's name table) or its index in config.json "images". Unknown
 * name: ReferenceError; no image at the index: RangeError.
 */
static int fb_image_arg(JSContext *ctx, int argc, JSValue *argv, int *index)
{
    if (argc < 1) {
        JS_ThrowTypeError(ctx, "fb.main.image: an image name is required");
        return -1;
    }
    if (JS_IsString(ctx, argv[0])) {
        JSCStringBuf buf;
        size_t len;
        const char *name = JS_ToCStringLen(ctx, &len, argv[0], &buf);
        if (!name)
            return -1;
        *index = image_hw_find(name, len);
        if (*index < 0) {
            JS_ThrowReferenceError(ctx, "unknown image \"%.32s\"", name);
            return -1;
        }
        return 0;
    }
    if (JS_ToInt32(ctx, index, argv[0]))
        return -1;
    if (!image_hw_exists(*index)) {
        JS_ThrowRangeError(ctx, "no image %d", *index);
        return -1;
    }
    return 0;
}

/*
 * fb.main.image.setIcon(image, icon) / setSrc(image, key). The string goes
 * straight into the image's fixed slot (no heap); "" shows nothing.
 */
static JSValue fb_image_set(JSContext *ctx, int method, int argc, JSValue *argv)
{
    int index = 0;
    if (fb_image_arg(ctx, argc, argv, &index))
        return JS_EXCEPTION;
    if (argc < 2)
        return JS_ThrowTypeError(ctx, method == FB_METHOD_IMAGE_SET_ICON
                                          ? "fb.main.image.setIcon() requires an icon name"
                                          : "fb.main.image.setSrc() requires a key");
    JSCStringBuf buf;
    size_t len;
    const char *value = JS_ToCStringLen(ctx, &len, argv[1], &buf);
    if (!value)
        return JS_EXCEPTION;
    const int r = method == FB_METHOD_IMAGE_SET_ICON ? image_hw_set_icon(index, value, len)
                                                     : image_hw_set_src(index, value, len);
    if (r == -2)
        return JS_ThrowTypeError(ctx, method == FB_METHOD_IMAGE_SET_ICON ? "invalid icon name \"%.32s\""
                                                                         : "key \"%.32s...\" is too long", value);
    return JS_UNDEFINED;
}

/*
 * fb.main.list.setItems(list, items): items is an array of strings (other
 * values are converted). The texts go straight into a builder in the
 * general heap; argv[1] is re-read for every element (a GC may move it).
 */
static JSValue fb_list_set_items(JSContext *ctx, int index, int argc, JSValue *argv)
{
    if (argc < 2 || !JS_IsArray(ctx, argv[1]))
        return JS_ThrowTypeError(ctx, "fb.main.list.setItems() requires an array of strings");
    int count = 0;
    if (JS_ToInt32(ctx, &count, JS_GetPropertyStr(ctx, argv[1], "length")))
        return JS_EXCEPTION;
    void *builder = list_hw_items_new();
    if (!builder)
        return JS_ThrowOutOfMemory(ctx);
    for (int i = 0; i < count; i++) {
        JSValue item = JS_GetPropertyUint32(ctx, argv[1], (uint32_t)i);
        if (JS_IsException(item)) {
            list_hw_items_free(builder);
            return JS_EXCEPTION;
        }
        JSCStringBuf buf;
        size_t len;
        const char *text = JS_ToCStringLen(ctx, &len, item, &buf);
        if (!text) {
            list_hw_items_free(builder);
            return JS_EXCEPTION;
        }
        if (!list_hw_items_add(builder, text, len))
            break;  /* full: the rest is left out */
    }
    return JS_NewBool(list_hw_items_commit(index, builder));
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
    case FB_METHOD_DISP_SET_ICON: {
        if (argc < 1) return JS_ThrowTypeError(ctx, "setIcon requires an icon name");
        JSValue tmp[2];
        tmp[0] = JS_NewInt32(ctx, pos);
        tmp[1] = argv[0];
        return js_freebutton_button_setIcon(ctx, this_val, 2, tmp);
    }
    case FB_METHOD_DISP_SET_FONT:
    case FB_METHOD_DISP_SET_TOP_FONT: {
        const int top = method == FB_METHOD_DISP_SET_TOP_FONT;
        int face = 0, size = 0;
        const int r = fb_font_args(ctx, argc, argv, top ? "setTopFont" : "setFont", &face, &size);
        if (r < 0)
            return JS_EXCEPTION;
        if ((r == FB_FONT_RESTORE ? button_hw_restore_font(pos, top) : button_hw_set_font(pos, face, size, top)) != 0)
            return JS_ThrowInternalError(ctx, "failed to set font for button %d", pos);
        return JS_UNDEFINED;
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
    case FB_METHOD_DITEM_SET_ICON:
        return js_freebutton_display_setIcon(ctx, this_val, argc, argv);
    case FB_METHOD_DITEM_SET_FONT:
        return js_freebutton_display_setFont(ctx, this_val, argc, argv);
    case FB_METHOD_IMAGE_SET_ICON:
    case FB_METHOD_IMAGE_SET_SRC:
        return fb_image_set(ctx, method, argc, argv);
    case FB_METHOD_DITEM_SHOW_PAGE:
        return js_freebutton_display_showPage(ctx, this_val, argc, argv);
    case FB_METHOD_DITEM_GET_SHOWN_PAGE:
        return js_freebutton_display_getShownPage(ctx, this_val, argc, argv);
    case FB_METHOD_LIST_NEXT:
    case FB_METHOD_LIST_PREV:
    case FB_METHOD_LIST_SELECT:
    case FB_METHOD_LIST_GET_SELECTED:
    case FB_METHOD_LIST_COUNT:
    case FB_METHOD_LIST_SET_ITEMS:
    case FB_METHOD_LIST_SET_FONT: {
        int index = 0;
        if (fb_list_arg(ctx, argc, argv, &index))
            return JS_EXCEPTION;
        switch (method) {
        case FB_METHOD_LIST_NEXT:
            return JS_NewInt32(ctx, list_hw_move(index, 1));
        case FB_METHOD_LIST_PREV:
            return JS_NewInt32(ctx, list_hw_move(index, -1));
        case FB_METHOD_LIST_SELECT: {
            int item = 0;
            if (argc < 2 || JS_ToInt32(ctx, &item, argv[1]))
                return argc < 2 ? JS_ThrowTypeError(ctx, "fb.main.list.select() requires an index") : JS_EXCEPTION;
            return JS_NewBool(list_hw_select(index, item));
        }
        case FB_METHOD_LIST_GET_SELECTED:
            return JS_NewInt32(ctx, list_hw_get_selected(index));
        case FB_METHOD_LIST_COUNT:
            return JS_NewInt32(ctx, list_hw_count(index));
        case FB_METHOD_LIST_SET_FONT: {
            int face = 0, size = 0;
            const int r = fb_font_args(ctx, argc - 1, argv + 1, "fb.main.list.setFont", &face, &size);
            if (r < 0)
                return JS_EXCEPTION;
            if (r == FB_FONT_RESTORE)
                list_hw_restore_font(index);
            else
                list_hw_set_font(index, face, size);
            return JS_UNDEFINED;
        }
        default:
            return fb_list_set_items(ctx, index, argc, argv);
        }
    }
    case FB_METHOD_KEY: {
        /* The name is looked up in the firmware's name table (config.json
         * "name" on a "buttons" entry); it is not copied into the JS heap. */
        if (argc < 1 || !JS_IsString(ctx, argv[0]))
            return JS_ThrowTypeError(ctx, "fb.key() requires a key name (string)");
        JSCStringBuf buf;
        size_t len;
        const char *name = JS_ToCStringLen(ctx, &len, argv[0], &buf);
        if (!name)
            return JS_EXCEPTION;
        int position = 0;
        if (!button_hw_find_by_name(name, len, &position))
            return JS_ThrowReferenceError(ctx, "unknown key \"%.32s\"", name);
        return fb_key_object(ctx, position);
    }
    default:
        return JS_ThrowInternalError(ctx, "unknown fb method %d", method);
    }
}
