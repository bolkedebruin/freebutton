/*
 * The style argument of the setStyle methods (a key's display,
 * fb.main.display, fb.main.list) and of fb.style.set: the name of a named
 * style of config.json "style.styles".
 *
 *   style: a string. "" = no named style on that part. Unknown:
 *          ReferenceError; not a string: TypeError; a style that sets
 *          bg_color / bg_opa where the part has no background (a key, a
 *          display item): TypeError.
 *
 * The name is looked up in the firmware's style table, on the calling task
 * (the table is read-only after boot); it is not copied into the JS heap.
 */
#ifndef FB_STYLE_ARG_H
#define FB_STYLE_ARG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "mquickjs.h"

#ifdef ESP_PLATFORM
#include "../style_binding.h"
#else
/* Host build stubs for the generator */
enum {
    STYLE_HW_SET_OK = 0,
    STYLE_HW_SET_UNKNOWN = -1,
    STYLE_HW_SET_NOT_IN_STYLE = -2,
    STYLE_HW_SET_FIXED = -3,
    STYLE_HW_SET_BAD_VALUE = -4,
    STYLE_HW_SET_NO_MEMORY = -5,
};
static inline int style_hw_find(const char *name, size_t len) { (void)name; (void)len; return 0; }
static inline bool style_hw_has_bg(int ref) { (void)ref; return false; }
static inline int style_hw_set_string(int ref, const char *prop, const char *value) { (void)ref; (void)prop; (void)value; return STYLE_HW_SET_UNKNOWN; }
static inline int style_hw_set_number(int ref, const char *prop, int32_t value) { (void)ref; (void)prop; (void)value; return STYLE_HW_SET_UNKNOWN; }
#endif

/* argv[0] = style. Returns 0 with *ref set (the style's ref, 0 for ""), or
 * -1 with an exception pending. `what` names the method in messages
 * ("setStyle"); allow_bg: the part is a list's. */
static inline int fb_style_arg(JSContext *ctx, int argc, JSValue *argv, const char *what, int allow_bg, int *ref)
{
    if (argc < 1 || !JS_IsString(ctx, argv[0])) {
        JS_ThrowTypeError(ctx, "%s() requires a style name (string)", what);
        return -1;
    }
    JSCStringBuf buf;
    size_t len;
    const char *name = JS_ToCStringLen(ctx, &len, argv[0], &buf);
    if (!name)
        return -1;
    if (len == 0) {
        *ref = 0;
        return 0;
    }
    *ref = style_hw_find(name, len);
    if (*ref == 0) {
        JS_ThrowReferenceError(ctx, "unknown style \"%.32s\"", name);
        return -1;
    }
    if (!allow_bg && style_hw_has_bg(*ref)) {
        JS_ThrowTypeError(ctx, "%s(): style \"%.32s\" sets a background, which only a list has", what, name);
        return -1;
    }
    return 0;
}

#endif /* FB_STYLE_ARG_H */
