/*
 * FreeButton DisplayItem JavaScript Bindings
 *
 * Provides JavaScript API for creating and manipulating display items
 * through the FreeButton UI system.
 *
 * JavaScript API:
 *   display.create(x, y, width, boxType, fontSize, alignment, label, unit, round, page)
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
 *   display.setFont(id, font, size) - Set the font of value and unit
 *   display.setStyle(id, style)  - Set the named style of value and unit
 *   display.setLabelStyle(id, style) - Set the named style of the caption
 *       (both exist only when config.json has named styles, "style.styles")
 *   display.setPosition(id, x, y) - Set display item position
 *   display.setPage(id, page)    - Set display item page
 *   display.showPage(page)       - Show a page of the main display
 *   display.getShownPage()       - Page of the main display shown now
 */

#include <stdio.h>
#include <stdlib.h>
#include "mquickjs.h"
#include "font_arg.h"
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
    int x, y, width, boxType, fontSize, alignment;
    const char* label;
    const char* unit;
    int round, page;
} DisplayItemInfo;
static inline unsigned long displayitem_create(int x, int y, int w, int bt, int fs, int al, const char* l, const char* u, int r, int p) { return 0; }
static inline bool displayitem_remove(unsigned long id) { return false; }
static inline int displayitem_count(void) { return 0; }
static inline const DisplayItemInfo* displayitem_get_info(unsigned long id) { return NULL; }
static inline bool displayitem_set_label(unsigned long id, const char* l) { return false; }
static inline bool displayitem_set_value(unsigned long id, const char* v) { return false; }
static inline bool displayitem_set_unit(unsigned long id, const char* u) { return false; }
static inline int displayitem_set_icon(unsigned long id, const char* n, size_t len) { return 0; }
static inline bool displayitem_set_font(unsigned long id, int face, int size) { return false; }
static inline bool displayitem_restore_font(unsigned long id) { return false; }
static inline bool displayitem_set_style(unsigned long id, int ref, bool caption) { return false; }
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
 * @param {number} fontSize - Font size
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
    int fontSize = 14, alignment = 0, round = 0, page = 1;
    const char *label = "";
    const char *unit = "";
    JSCStringBuf labelBuf, unitBuf;

    if (argc < 10) {
        return JS_ThrowTypeError(ctx, "display.create() requires 10 arguments: x, y, width, boxType, fontSize, alignment, label, unit, round, page");
    }

    if (JS_ToInt32(ctx, &x, argv[0])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &y, argv[1])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &width, argv[2])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &boxType, argv[3])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &fontSize, argv[4])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &alignment, argv[5])) return JS_EXCEPTION;

    size_t labelLen, unitLen;
    label = JS_ToCStringLen(ctx, &labelLen, argv[6], &labelBuf);
    if (!label) return JS_EXCEPTION;

    unit = JS_ToCStringLen(ctx, &unitLen, argv[7], &unitBuf);
    if (!unit) return JS_EXCEPTION;

    if (JS_ToInt32(ctx, &round, argv[8])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &page, argv[9])) return JS_EXCEPTION;

    unsigned long id = displayitem_create(x, y, width, boxType, fontSize, alignment, label, unit, round, page);

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
 * @returns {object} Display item info with properties: id, x, y, width, boxType, fontSize, alignment, label, unit, round, page
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

    /* Create a JavaScript object with display item information */
    JSValue obj = JS_NewObject(ctx);

    JS_SetPropertyStr(ctx, obj, "id", JS_NewInt32(ctx, (int32_t)info->id));
    JS_SetPropertyStr(ctx, obj, "x", JS_NewInt32(ctx, info->x));
    JS_SetPropertyStr(ctx, obj, "y", JS_NewInt32(ctx, info->y));
    JS_SetPropertyStr(ctx, obj, "width", JS_NewInt32(ctx, info->width));
    JS_SetPropertyStr(ctx, obj, "boxType", JS_NewInt32(ctx, info->boxType));
    JS_SetPropertyStr(ctx, obj, "fontSize", JS_NewInt32(ctx, info->fontSize));
    JS_SetPropertyStr(ctx, obj, "alignment", JS_NewInt32(ctx, info->alignment));
    JS_SetPropertyStr(ctx, obj, "label", JS_NewString(ctx, info->label ? info->label : ""));
    JS_SetPropertyStr(ctx, obj, "unit", JS_NewString(ctx, info->unit ? info->unit : ""));
    JS_SetPropertyStr(ctx, obj, "round", JS_NewInt32(ctx, info->round));
    JS_SetPropertyStr(ctx, obj, "page", JS_NewInt32(ctx, info->page));

    return obj;
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

    JSValue arr = JS_NewArray(ctx, count);

    for (int i = 0; i < count; i++) {
        JS_SetPropertyUint32(ctx, arr, i, JS_NewInt32(ctx, (int32_t)ids[i]));
    }

    return arr;
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
 * @jsapi display.setFont
 * @description Set the font of the display item's value and unit (config
 *        "font" / "fontsize"); the caption keeps its own. Font and size
 *        escapes in the text still apply on top of it.
 * @param {number|string} id - Display item ID, or its config name
 * @param {string|number} font - A config.json "style.fonts" name or id, or a system font (montserrat, montserrat_semibold); "" = back to the font and size of config.json (or the style's)
 * @param {number} size - 1..5 = the legacy steps (14/24/32/48/72 px), 6..72 = px (ignored for "")
 * @returns {boolean} True if successful, false if the item does not exist
 */
JSValue js_freebutton_display_setFont(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    int id = 0;
    int face = 0;
    int size = 0;

    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "display.setFont() requires id, font and size arguments");
    }

    if (displayitem_arg_id(ctx, &id, argv[0]))
        return JS_EXCEPTION;
    const int r = fb_font_args(ctx, argc - 1, argv + 1, "display.setFont", &face, &size);
    if (r < 0)
        return JS_EXCEPTION;
    if (r == FB_FONT_RESTORE)
        return JS_NewBool(displayitem_restore_font((unsigned long)id));
    return JS_NewBool(displayitem_set_font((unsigned long)id, face, size));
}

/* setStyle (caption = 0) / setLabelStyle (caption = 1). */
static JSValue display_set_style(JSContext *ctx, int argc, JSValue *argv, int caption)
{
    const char *what = caption ? "display.setLabelStyle" : "display.setStyle";
    int id = 0;
    int ref = 0;

    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "%s() requires id and style arguments", what);
    }
    if (displayitem_arg_id(ctx, &id, argv[0]))
        return JS_EXCEPTION;
    /* a display item has no background: no style with bg_* */
    if (fb_style_arg(ctx, argc - 1, argv + 1, what, 0, &ref))
        return JS_EXCEPTION;
    return JS_NewBool(displayitem_set_style((unsigned long)id, ref, caption != 0));
}

/**
 * @jsapi display.setStyle
 * @description Give the display item's value and unit a named style of
 *        config.json "style.styles"; "" takes the named style away. The
 *        item's own fields (font, fontsize) and text markup still apply on
 *        top of it. Exists only when the config has named styles.
 * @param {number|string} id - Display item ID, or its config name
 * @param {string} style - A style name, or "" for none
 * @returns {boolean} True if successful, false if the item does not exist
 */
JSValue js_freebutton_display_setStyle(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    return display_set_style(ctx, argc, argv, 0);
}

/**
 * @jsapi display.setLabelStyle
 * @description Give the display item's caption a named style of config.json
 *        "style.styles"; "" takes the named style away. Exists only when
 *        the config has named styles.
 * @param {number|string} id - Display item ID, or its config name
 * @param {string} style - A style name, or "" for none
 * @returns {boolean} True if successful, false if the item does not exist
 */
JSValue js_freebutton_display_setLabelStyle(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    return display_set_style(ctx, argc, argv, 1);
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
