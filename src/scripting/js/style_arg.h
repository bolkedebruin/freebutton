/*
 * The style arguments of the script API.
 *
 * setStyle (a button's display, fb.main.display, fb.main.list) and
 * display.create take a string of style names of config.json "styles", as
 * an item's "style" field has it:
 *
 *   styles: a string of up to four names separated by spaces ("big alarm").
 *           "" = the list config.json gives the item. Not a string:
 *           TypeError; an unknown style: ReferenceError; "button",
 *           "displayitem" or "list" (the style of a kind, which every item
 *           of that kind has anyway): TypeError; a fifth name: RangeError;
 *           a NUL character in the string: TypeError.
 *
 * addStyle and removeStyle (a button's display, fb.main.display) take one
 * name:
 *
 *   style:  a string that is one style name. Not a string, or "":
 *           TypeError; an unknown style: ReferenceError; the style of a
 *           kind: TypeError; a NUL character in the name: TypeError.
 *
 * The names are looked up in the firmware's style table, on the calling
 * task (the table is read-only after boot); nothing is copied into the JS
 * heap.
 */
#ifndef FB_STYLE_ARG_H
#define FB_STYLE_ARG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "mquickjs.h"

#ifdef ESP_PLATFORM
#include "../style_binding.h"
#else
/* Host build stubs for the generator */
enum {
    STYLE_HW_LIST_OK = 0,
    STYLE_HW_LIST_UNKNOWN = -1,
    STYLE_HW_LIST_RESERVED = -2,
    STYLE_HW_LIST_TOO_MANY = -3,
};
enum {
    STYLE_HW_SET_OK = 0,
    STYLE_HW_SET_UNKNOWN = -1,
    STYLE_HW_SET_NO_PART = -2,
    STYLE_HW_SET_FIXED = -3,
    STYLE_HW_SET_BAD_VALUE = -4,
    STYLE_HW_SET_NO_MEMORY = -5,
};
#define STYLE_HW_LIST_TEXT_MAX 100
static inline int style_hw_parse_list(const char *text, uint32_t *list, const char **bad, size_t *bad_len) { (void)text; (void)list; (void)bad; (void)bad_len; return STYLE_HW_LIST_OK; }
static inline size_t style_hw_list_text(uint32_t list, char *out, size_t capacity) { (void)list; if (capacity) out[0] = 0; return 0; }
static inline int style_hw_find(const char *name, size_t len) { (void)name; (void)len; return 0; }
static inline int style_hw_set_string(int ref, const char *prop, const char *value) { (void)ref; (void)prop; (void)value; return STYLE_HW_SET_UNKNOWN; }
static inline int style_hw_set_number(int ref, const char *prop, double value) { (void)ref; (void)prop; (void)value; return STYLE_HW_SET_UNKNOWN; }
#endif

/* `value` = styles. Returns 0 with *list set and *restore = 1 for a string
 * without a name ("": the caller gives the item its configured list), or -1
 * with an exception pending. `what` names the method in messages
 * ("setStyle"). */
static inline int fb_style_list_arg(JSContext *ctx, JSValue value, const char *what, uint32_t *list, int *restore)
{
    if (!JS_IsString(ctx, value)) {
        JS_ThrowTypeError(ctx, "%s() requires style names (a string)", what);
        return -1;
    }
    JSCStringBuf buf;
    size_t len;
    const char *text = JS_ToCStringLen(ctx, &len, value, &buf);
    if (!text)
        return -1;
    /* The names are read as a C string, which ends at a NUL: what comes
     * after it must not be dropped silently ("big\0junk" is not "big"). */
    if (strlen(text) != len) {
        JS_ThrowTypeError(ctx, "%s(): the style names have a NUL character in them", what);
        return -1;
    }
    const char *bad = "";
    size_t bad_len = 0;
    *list = 0;
    const int result = style_hw_parse_list(text, list, &bad, &bad_len);
    if (result == STYLE_HW_LIST_OK) {
        *restore = *list == 0;
        return 0;
    }
    /* The name the error is about, as a C string (it is a part of `text`). */
    char name[33];
    if (bad_len > sizeof(name) - 1)
        bad_len = sizeof(name) - 1;
    memcpy(name, bad, bad_len);
    name[bad_len] = '\0';
    switch (result) {
    case STYLE_HW_LIST_RESERVED:
        JS_ThrowTypeError(ctx, "%s(): \"%s\" is the style of a kind and cannot be named by an item", what, name);
        return -1;
    case STYLE_HW_LIST_TOO_MANY:
        JS_ThrowRangeError(ctx, "%s(): an item has at most 4 styles", what);
        return -1;
    default:
        JS_ThrowReferenceError(ctx, "unknown style \"%s\"", name);
        return -1;
    }
}

/* `value` = style, one name. Returns 0 with *ref set (>= 1), or -1 with an
 * exception pending. `what` names the method in messages ("addStyle"). */
static inline int fb_style_name_arg(JSContext *ctx, JSValue value, const char *what, int *ref)
{
    if (!JS_IsString(ctx, value)) {
        JS_ThrowTypeError(ctx, "%s() requires a style name (a string)", what);
        return -1;
    }
    JSCStringBuf buf;
    size_t len;
    const char *name = JS_ToCStringLen(ctx, &len, value, &buf);
    if (!name)
        return -1;
    if (len == 0) {
        JS_ThrowTypeError(ctx, "%s() requires a style name (a string)", what);
        return -1;
    }
    if (strlen(name) != len) {
        JS_ThrowTypeError(ctx, "%s(): the style name has a NUL character in it", what);
        return -1;
    }
    *ref = style_hw_find(name, len);
    if (*ref > 0)
        return 0;
    /* The name for the message, on the stack: `name` may point into the JS
     * string, and making the error allocates. */
    char shown[33];
    if (len > sizeof(shown) - 1)
        len = sizeof(shown) - 1;
    memcpy(shown, name, len);
    shown[len] = '\0';
    if (*ref < 0)
        JS_ThrowTypeError(ctx, "%s(): \"%s\" is the style of a kind and cannot be named by an item", what, shown);
    else
        JS_ThrowReferenceError(ctx, "unknown style \"%s\"", shown);
    return -1;
}

#endif /* FB_STYLE_ARG_H */
