//
// seam_fonts.c - the firmware's font registry (src/ui/fonts/fonts.c) as it
// is, plus what the host's records read from its tables: which face and size
// an lv_font_t is, and the faces. fonts.c keeps both private.
//
#include "ui/fonts/fonts.c"

#include "emu_ui.h"

#include "emu.h"

/* The face and px of a font fonts_get() gave out; false for any other font
 * (LVGL's built-in one). `real`: drawn from its TrueType file, not the
 * built-in font standing in for a missing or invalid file. */
bool fbe_font_info(const lv_font_t *font, int *face, int *px, bool *real) {
    for (size_t i = 0; i < inst_count; i++) {
        if (&inst[i].wrap == font) {
            *face = inst[i].face;
            *px = inst[i].px;
            *real = inst[i].ttf != NULL;
            return true;
        }
    }
    return false;
}

size_t fbe_font_face_count(void) { return face_count; }

bool fbe_font_face(const size_t index, const char **name, const char **key, int32_t *id, int *fallback,
                   int *lineheight_x100, bool *usable) {
    if (index >= face_count) return false;
    *name = faces[index].name;
    *key = faces[index].key;
    *id = faces[index].id;
    *fallback = faces[index].fallback;
    *lineheight_x100 = faces[index].lineheight_x100;
    *usable = faces[index].usable;
    return true;
}

size_t fbe_font_instance_count(void) { return inst_count; }

/* fonts.c polls for reload requests every 250 ms (the request comes from
 * another task there). Here a request only comes from the host, between two
 * turns of the LVGL task, so the poll sleeps while there is none: an idle
 * emulator has no timer. Called at the start of each turn. */
void fbe_fonts_poll_sleep(void) {
    if (reload_timer == NULL) return;
    const bool idle = !reload_pending && atomic_load_explicit(&reload_requests, memory_order_relaxed) == seen_requests;
    if (idle) lv_timer_pause(reload_timer);
    else lv_timer_resume(reload_timer);
}
