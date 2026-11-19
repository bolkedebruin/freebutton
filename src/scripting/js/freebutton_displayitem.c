/*
 * FreeButton DisplayItem JavaScript Bindings
 *
 * Provides JavaScript API for creating and manipulating display items
 * through the FreeButton UI system.
 *
 * JavaScript API:
 *   display.create(x, y, width, boxType, styles, alignment, label, unit, round, page)
 *       - Create a new display item, returns ID
 *   (id: a display item id, or the item's config.json "name")
 *   display.remove(id)           - Remove a display item
 *   display.count()              - Get number of display items
 *   display.get(id)              - Get display item info
 *   display.getAll()             - Get array of all display item IDs
 *   display.setLabel(id, label)  - Set display item label
 *   display.setValue(id, value)  - Set display item value
 *   display.setUnit(id, unit)    - Set display item unit
 *   display.setIcon(id, name)    - Set display item icon ("" hides it)
 *   display.setStyle(id, styles) - Set the item's styles (config.json "styles")
 *   display.addStyle(id, style)  - Add one style as the last of the item's styles
 *   display.removeStyle(id, style) - Take one style out of the item's styles
 *   display.setPosition(id, x, y) - Set display item position
 *   display.setPage(id, page)    - Set display item page
 *   display.showPage(page)       - Show a page of the main display
 *   display.getShownPage()       - Page of the main display shown now
 */

#include <stdio.h>
#include <stdlib.h>
#include "mquickjs.h"
#include "style_arg.h"

/* ESP32-specific includes */
#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "../displayitem_binding.h"
#undef TAG
#define TAG "DisplayItemJS"
#else
/* Host build stubs for generator */
#include <stdbool.h>
#define ESP_LOGI(tag, ...)
#define ESP_LOGE(tag, ...)
#define ESP_LOGW(tag, ...)
typedef struct {
    unsigned long id;
    int x, y, width, boxType, alignment;
    uint32_t styles;
    const char* label;
    const char* unit;
    int round, page;
} DisplayItemInfo;
static inline unsigned long displayitem_create(int x, int y, int w, int bt, uint32_t styles, int al, const char* l, const char* u, int r, int p) { return 0; }
static inline bool displayitem_remove(unsigned long id) { return false; }
static inline int displayitem_count(void) { return 0; }
static inline const DisplayItemInfo* displayitem_get_info(unsigned long id) { return NULL; }
static inline bool displayitem_set_label(unsigned long id, const char* l) { return false; }
static inline bool displayitem_set_value(unsigned long id, const char* v) { return false; }
static inline bool displayitem_set_unit(unsigned long id, const char* u) { return false; }
static inline int displayitem_set_icon(unsigned long id, const char* n, size_t len) { return 0; }
static inline bool displayitem_set_styles(unsigned long id, uint32_t list, bool restore) { return false; }
static inline int displayitem_add_style(unsigned long id, int ref) { return 0; }
static inline int displayitem_remove_style(unsigned long id, int ref) { return 0; }
static inline bool displayitem_set_position(unsigned long id, int x, int y) { return false; }
static inline bool displayitem_set_page(unsigned long id, int p) { return false; }
static inline bool displayitem_show_page(int p) { return p >= 1; }
static inline int displayitem_get_shown_page(void) { return 1; }
static inline int displayitem_get_all_ids(unsigned long* ids, int max) { return 0; }
static inline bool displayitem_find_by_name(const char* n, size_t len, unsigned long* id) { return false; }
#endif

/* Maximum number of display items for iteration */
#define MAX_DISPLAY_ITEMS 32

/*
 * JavaScript bindings
 */

/*
 * First argument of the item methods: a display item id (number) or the
 * item's config name (string, config.json "name"). The name is looked up in
 * the firmware's name table; it is not copied into the JS heap. An unknown
 * name throws a ReferenceError (a typo should not fail silently); an unknown
 * numeric id keeps returning false as before.
 */
static int displayitem_arg_id(JSContext *ctx, int *id, JSValue val)
{
    if (!JS_IsString(ctx, val))
        return JS_ToInt32(ctx, id, val);
    JSCStringBuf buf;
    size_t len;
    const char *name = JS_ToCStringLen(ctx, &len, val, &buf);
    if (!name)
        return -1;
    unsigned long found = 0;
    if (!displayitem_find_by_name(name, len, &found)) {
        JS_ThrowReferenceError(ctx, "unknown display item \"%.32s\"", name);
        return -1;
    }
    *id = (int)found;
    return 0;
}

/**
 * @jsapi display.create
 * @description Create a new display item on the screen
 * @param {number} x - X position
 * @param {number} y - Y position
 * @param {number} width - Width of the display item
 * @param {number} boxType - Box type (0-3)
 * @param {string} styles - Style names of config.json "styles", separated by spaces ("" for none)
 * @param {number} alignment - Text alignment (0=left, 1=center, 2=right)
 * @param {string} label - Label text
 * @param {string} unit - Unit text
 * @param {number} round - Decimal rounding
 * @param {number} page - Page number
 * @returns {number} ID of the created display item, or 0 on failure
 */
JSValue js_freebutton_display_create(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    int x = 0, y = 0, width = 100, boxType = 0;
    int alignment = 0, round = 0, page = 1;
    uint32_t styles = 0;
    int noStyles = 0;
    const char *label = "";
    const char *unit = "";
    JSCStringBuf labelBuf, unitBuf;

    if (argc < 10) {
        return JS_ThrowTypeError(ctx, "display.create() requires 10 arguments: x, y, width, boxType, styles, alignment, label, unit, round, page");
    }

    if (JS_ToInt32(ctx, &x, argv[0])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &y, argv[1])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &width, argv[2])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &boxType, argv[3])) return JS_EXCEPTION;
    /* the style names: resolved to refs here, before the texts are read */
    if (fb_style_list_arg(ctx, argv[4], "display.create", &styles, &noStyles)) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &alignment, argv[5])) return JS_EXCEPTION;

    size_t labelLen, unitLen;
    label = JS_ToCStringLen(ctx, &labelLen, argv[6], &labelBuf);
    if (!label) return JS_EXCEPTION;

    unit = JS_ToCStringLen(ctx, &unitLen, argv[7], &unitBuf);
    if (!unit) return JS_EXCEPTION;

    if (JS_ToInt32(ctx, &round, argv[8])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &page, argv[9])) return JS_EXCEPTION;

    unsigned long id = displayitem_create(x, y, width, boxType, styles, alignment, label, unit, round, page);

    if (id == 0) {
        return JS_ThrowInternalError(ctx, "Failed to create display item");
    }

    return JS_NewInt32(ctx, (int32_t)id);
}

/**
 * @jsapi display.remove
 * @description Remove a display item by ID
 * @param {number|string} id - Display item ID, or its config name
 * @returns {boolean} True if removed, false if not found
 */
JSValue js_freebutton_display_remove(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    int id = 0;

    if (argc >= 1) {
        if (displayitem_arg_id(ctx, &id, argv[0]))
            return JS_EXCEPTION;
    } else {
        return JS_ThrowTypeError(ctx, "display.remove() requires id argument");
    }

    bool removed = displayitem_remove((unsigned long)id);
    return JS_NewBool(removed);
}

/**
 * @jsapi display.count
 * @description Get number of display items
 * @returns {number} Number of display items
 */
JSValue js_freebutton_display_count(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    int count = displayitem_count();
    return JS_NewInt32(ctx, count);
}

/**
 * @jsapi display.get
 * @description Get display item information by ID
 * @param {number|string} id - Display item ID, or its config name
 * @returns {object} Display item info with properties: id, x, y, width, boxType, style, alignment, label, unit, round, page
 */
JSValue js_freebutton_display_get(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    int id = 0;

    if (argc >= 1) {
        if (displayitem_arg_id(ctx, &id, argv[0]))
            return JS_EXCEPTION;
    } else {
        return JS_ThrowTypeError(ctx, "display.get() requires id argument");
    }

    const DisplayItemInfo* info = displayitem_get_info((unsigned long)id);
    if (!info) {
        return JS_NULL;
    }

    /* The object under construction is kept in a GC ref: the engine's
     * collector moves objects, and every call below allocates (the property
     * key, the property table, a string value), so a plain JSValue local
     * would point at the object's old place after the first of them. Each
     * value is made first, then *obj is read for the call (never both in
     * one argument list: the order of evaluation is not defined). */
    JSGCRef obj_ref;
    JSValue *obj = JS_PushGCRef(ctx, &obj_ref);
    JSValue v;
    *obj = JS_NewObject(ctx);
    if (JS_IsException(*obj)) {
        JS_PopGCRef(ctx, &obj_ref);
        return JS_EXCEPTION;
    }

    v = JS_NewInt32(ctx, (int32_t)info->id);
    JS_SetPropertyStr(ctx, *obj, "id", v);
    v = JS_NewInt32(ctx, info->x);
    JS_SetPropertyStr(ctx, *obj, "x", v);
    v = JS_NewInt32(ctx, info->y);
    JS_SetPropertyStr(ctx, *obj, "y", v);
    v = JS_NewInt32(ctx, info->width);
    JS_SetPropertyStr(ctx, *obj, "width", v);
    v = JS_NewInt32(ctx, info->boxType);
    JS_SetPropertyStr(ctx, *obj, "boxType", v);
    {
        /* the item's styles now, as config.json writes a "style" field */
        char names[STYLE_HW_LIST_TEXT_MAX];
        style_hw_list_text(info->styles, names, sizeof(names));
        v = JS_NewString(ctx, names);
        JS_SetPropertyStr(ctx, *obj, "style", v);
    }
    v = JS_NewInt32(ctx, info->alignment);
    JS_SetPropertyStr(ctx, *obj, "alignment", v);
    v = JS_NewString(ctx, info->label ? info->label : "");
    JS_SetPropertyStr(ctx, *obj, "label", v);
    v = JS_NewString(ctx, info->unit ? info->unit : "");
    JS_SetPropertyStr(ctx, *obj, "unit", v);
    v = JS_NewInt32(ctx, info->round);
    JS_SetPropertyStr(ctx, *obj, "round", v);
    v = JS_NewInt32(ctx, info->page);
    JS_SetPropertyStr(ctx, *obj, "page", v);

    return JS_PopGCRef(ctx, &obj_ref);
}

/**
 * @jsapi display.getAll
 * @description Get array of all display item IDs
 * @returns {number[]} Array of display item IDs
 */
JSValue js_freebutton_display_getAll(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    unsigned long ids[MAX_DISPLAY_ITEMS];
    int count = displayitem_get_all_ids(ids, MAX_DISPLAY_ITEMS);

    /* In a GC ref: storing an element may allocate, and the collector
     * moves the array (see display.get). */
    JSGCRef arr_ref;
    JSValue *arr = JS_PushGCRef(ctx, &arr_ref);
    *arr = JS_NewArray(ctx, count);
    if (JS_IsException(*arr)) {
        JS_PopGCRef(ctx, &arr_ref);
        return JS_EXCEPTION;
    }

    for (int i = 0; i < count; i++) {
        JSValue v = JS_NewInt32(ctx, (int32_t)ids[i]);
        JS_SetPropertyUint32(ctx, *arr, i, v);
    }

    return JS_PopGCRef(ctx, &arr_ref);
}

/**
 * @jsapi display.setLabel
 * @description Set display item label text
 * @param {number|string} id - Display item ID, or its config name
 * @param {string} label - New label text
 * @returns {boolean} True if successful
 */
JSValue js_freebutton_display_setLabel(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    int id = 0;
    JSCStringBuf buf;

    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "display.setLabel() requires id and label arguments");
    }

    if (displayitem_arg_id(ctx, &id, argv[0]))
        return JS_EXCEPTION;

    size_t len;
    const char *label = JS_ToCStringLen(ctx, &len, argv[1], &buf);
    if (!label)
        return JS_EXCEPTION;

    bool success = displayitem_set_label((unsigned long)id, label);
    return JS_NewBool(success);
}

/**
 * @jsapi display.setValue
 * @description Set display item value text
 * @param {number|string} id - Display item ID, or its config name
 * @param {string} value - New value text
 * @returns {boolean} True if successful
 */
JSValue js_freebutton_display_setValue(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    int id = 0;
    JSCStringBuf buf;

    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "display.setValue() requires id and value arguments");
    }

    if (displayitem_arg_id(ctx, &id, argv[0]))
        return JS_EXCEPTION;

    size_t len;
    const char *value = JS_ToCStringLen(ctx, &len, argv[1], &buf);
    if (!value)
        return JS_EXCEPTION;

    bool success = displayitem_set_value((unsigned long)id, value);
    return JS_NewBool(success);
}

/**
 * @jsapi display.setUnit
 * @description Set display item unit text
 * @param {number|string} id - Display item ID, or its config name
 * @param {string} unit - New unit text
 * @returns {boolean} True if successful
 */
JSValue js_freebutton_display_setUnit(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    int id = 0;
    JSCStringBuf buf;

    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "display.setUnit() requires id and unit arguments");
    }

    if (displayitem_arg_id(ctx, &id, argv[0]))
        return JS_EXCEPTION;

    size_t len;
    const char *unit = JS_ToCStringLen(ctx, &len, argv[1], &buf);
    if (!unit)
        return JS_EXCEPTION;

    bool success = displayitem_set_unit((unsigned long)id, unit);
    return JS_NewBool(success);
}

/**
 * @jsapi display.setIcon
 * @description Show an icon (a name from config.json "icons") left of the
 *        display item's value; "" hides it. The web IDE renders the raster at
 *        the item's "iconsize" (default 32) when the config is saved; an icon
 *        without a raster is not shown.
 * @param {number|string} id - Display item ID, or its config name
 * @param {string} name - Icon name ([A-Za-z0-9_-], max 31), or "" to hide
 * @returns {boolean} True if successful, false if the item does not exist
 */
JSValue js_freebutton_display_setIcon(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    int id = 0;
    JSCStringBuf buf;

    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "display.setIcon() requires id and icon name arguments");
    }

    if (displayitem_arg_id(ctx, &id, argv[0]))
        return JS_EXCEPTION;

    size_t len;
    const char *name = JS_ToCStringLen(ctx, &len, argv[1], &buf);
    if (!name)
        return JS_EXCEPTION;

    const int result = displayitem_set_icon((unsigned long)id, name, len);
    if (result == -1)
        return JS_ThrowTypeError(ctx, "invalid icon name \"%.32s\"", name);
    if (result < 0)
        return JS_ThrowOutOfMemory(ctx);
    return JS_NewBool(result == 1);
}

/**
 * @jsapi display.setStyle
 * @description Give the display item a list of styles of config.json
 *        "styles"; "" gives it the list of config.json again. The value
 *        takes their main blocks, the caption their "title" blocks, the
 *        unit their "unit" blocks. Text markup still applies on top.
 * @param {number|string} id - Display item ID, or its config name
 * @param {string} styles - Up to four style names separated by spaces, or ""
 * @returns {boolean} True if successful, false if the item does not exist
 */
JSValue js_freebutton_display_setStyle(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    int id = 0;
    uint32_t list = 0;
    int restore = 0;

    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "display.setStyle() requires id and styles arguments");
    }
    if (displayitem_arg_id(ctx, &id, argv[0]))
        return JS_EXCEPTION;
    if (fb_style_list_arg(ctx, argv[1], "display.setStyle", &list, &restore))
        return JS_EXCEPTION;
    return JS_NewBool(displayitem_set_styles((unsigned long)id, list, restore != 0));
}

/**
 * @jsapi display.addStyle
 * @description Add one style of config.json "styles" as the last of the
 *        item's styles, so that it wins over the others. A style the item
 *        has already moves to the end. A fifth style throws a RangeError
 *        and changes nothing.
 * @param {number|string} id - Display item ID, or its config name
 * @param {string} style - A style name
 * @returns {boolean} True if successful, false if the item does not exist
 */
JSValue js_freebutton_display_addStyle(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    int id = 0;
    int ref = 0;

    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "display.addStyle() requires id and style arguments");
    }
    if (displayitem_arg_id(ctx, &id, argv[0]))
        return JS_EXCEPTION;
    if (fb_style_name_arg(ctx, argv[1], "display.addStyle", &ref))
        return JS_EXCEPTION;
    const int result = displayitem_add_style((unsigned long)id, ref);
    if (result < 0)
        return JS_ThrowRangeError(ctx, "display.addStyle(): an item has at most 4 styles");
    return JS_NewBool(result == 1);
}

/**
 * @jsapi display.removeStyle
 * @description Take one style out of the item's styles, also one that
 *        config.json gave it. A style the item does not have: nothing
 *        changes.
 * @param {number|string} id - Display item ID, or its config name
 * @param {string} style - A style name
 * @returns {boolean} True if successful, false if the item does not exist
 */
JSValue js_freebutton_display_removeStyle(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    int id = 0;
    int ref = 0;

    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "display.removeStyle() requires id and style arguments");
    }
    if (displayitem_arg_id(ctx, &id, argv[0]))
        return JS_EXCEPTION;
    if (fb_style_name_arg(ctx, argv[1], "display.removeStyle", &ref))
        return JS_EXCEPTION;
    return JS_NewBool(displayitem_remove_style((unsigned long)id, ref) == 1);
}

/**
 * @jsapi display.setPosition
 * @description Set display item position
 * @param {number|string} id - Display item ID, or its config name
 * @param {number} x - New X position
 * @param {number} y - New Y position
 * @returns {boolean} True if successful
 */
JSValue js_freebutton_display_setPosition(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    int id = 0, x = 0, y = 0;

    if (argc < 3) {
        return JS_ThrowTypeError(ctx, "display.setPosition() requires id, x, and y arguments");
    }

    if (displayitem_arg_id(ctx, &id, argv[0]))
        return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &x, argv[1]))
        return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &y, argv[2]))
        return JS_EXCEPTION;

    bool success = displayitem_set_position((unsigned long)id, x, y);
    return JS_NewBool(success);
}

/**
 * @jsapi display.setPage
 * @description Set display item page
 * @param {number|string} id - Display item ID, or its config name
 * @param {number} page - New page number
 * @returns {boolean} True if successful
 */
JSValue js_freebutton_display_setPage(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    int id = 0, page = 1;

    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "display.setPage() requires id and page arguments");
    }

    if (displayitem_arg_id(ctx, &id, argv[0]))
        return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &page, argv[1]))
        return JS_EXCEPTION;

    bool success = displayitem_set_page((unsigned long)id, page);
    return JS_NewBool(success);
}

/**
 * @jsapi display.showPage
 * @description Show a page of the main display. Items (and the clock) whose
 *              page is this page or 0 ("every page") are shown, the others
 *              are hidden. Pages count from 1; page 1 is shown at boot.
 * @param {number} page - Page to show (1 or more)
 */
JSValue js_freebutton_display_showPage(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    int page = 0;

    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "display.showPage() requires a page argument");
    }
    if (JS_ToInt32(ctx, &page, argv[0]))
        return JS_EXCEPTION;
    if (!displayitem_show_page(page)) {
        return JS_ThrowRangeError(ctx, "display.showPage(): page must be 1 or more");
    }
    return JS_UNDEFINED;
}

/**
 * @jsapi display.getShownPage
 * @description Page of the main display shown now (1 at boot)
 * @returns {number} Page number
 */
JSValue js_freebutton_display_getShownPage(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    return JS_NewInt32(ctx, displayitem_get_shown_page());
}
