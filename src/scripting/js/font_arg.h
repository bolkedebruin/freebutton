/*
 * The (font, size) arguments of the setFont / setTopFont methods
 * (fb.main.display, a key's display, fb.main.list).
 *
 *   font: a config.json "style.fonts" name (or a system one: montserrat,
 *         montserrat_semibold) or a font id. Unknown: ReferenceError; another type:
 *         TypeError. Resolved here, on the calling task (the face table is
 *         read-only after boot, fonts.h); the instance is made later by the
 *         LVGL task.
 *         "" (like setIcon("")): back to the font AND size of config.json
 *         (or the element's style when it sets none); size is then
 *         optional and ignored.
 *   size: an integer 1..72: 1..5 = the legacy steps (14/24/32/48/72 px),
 *         6..72 = px. Else TypeError.
 */
#ifndef FB_FONT_ARG_H
#define FB_FONT_ARG_H

#include <stddef.h>
#include <stdint.h>
#include "mquickjs.h"

#ifdef ESP_PLATFORM
#include "../../ui/fonts/fonts.h"
#else
/* Host build stubs for the generator */
#define FONT_FACE_NONE (-1)
#define FONT_PX_MAX 72
static inline int fonts_find(const char *name, size_t len) { (void)name; (void)len; return FONT_FACE_NONE; }
static inline int fonts_find_id(int32_t id) { (void)id; return FONT_FACE_NONE; }
#endif

/* argv[0] = font, argv[1] = size. Returns 0 (face and size set), 1 for ""
 * (restore the configured font; face and size untouched), or -1 with an
 * exception pending. `what` names the method in messages ("setFont"). */
#define FB_FONT_RESTORE 1
static inline int fb_font_args(JSContext *ctx, int argc, JSValue *argv, const char *what, int *face, int *size)
{
    if (argc < 1) {
        JS_ThrowTypeError(ctx, "%s() requires a font and a size", what);
        return -1;
    }
    if (JS_IsString(ctx, argv[0])) {
        JSCStringBuf buf;
        size_t len;
        const char *name = JS_ToCStringLen(ctx, &len, argv[0], &buf);
        if (!name)
            return -1;
        if (len == 0)
            return FB_FONT_RESTORE;
        *face = fonts_find(name, len);
        if (*face == FONT_FACE_NONE) {
            JS_ThrowReferenceError(ctx, "unknown font \"%.32s\"", name);
            return -1;
        }
    } else if (JS_IsNumber(ctx, argv[0])) {
        double d;
        if (JS_ToNumber(ctx, &d, argv[0]))
            return -1;
        const int32_t id = (d >= 1 && d <= INT32_MAX && d == (double)(int32_t)d) ? (int32_t)d : 0;
        *face = id > 0 ? fonts_find_id(id) : FONT_FACE_NONE;
        if (*face == FONT_FACE_NONE) {
            /* (mquickjs formats %d, not %g) */
            JS_ThrowReferenceError(ctx, "unknown font id %d", (d > -2147483648.0 && d < 2147483647.0) ? (int)d : 0);
            return -1;
        }
    } else {
        JS_ThrowTypeError(ctx, "%s(): the font must be a font name or id", what);
        return -1;
    }

    if (argc < 2) {
        JS_ThrowTypeError(ctx, "%s() requires a font and a size", what);
        return -1;
    }
    double s = 0;
    if (!JS_IsNumber(ctx, argv[1]) || JS_ToNumber(ctx, &s, argv[1]) || s < 1 || s > FONT_PX_MAX
        || s != (double)(int)s) {
        JS_ThrowTypeError(ctx, "%s(): the size must be an integer 1..%d", what, FONT_PX_MAX);
        return -1;
    }
    *size = (int)s;
    return 0;
}

#endif /* FB_FONT_ARG_H */
