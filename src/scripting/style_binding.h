//
// Style binding - C interface to the named styles (config "style.styles",
// ui/ui_style.h) for the JS API: the style argument of the setStyle methods
// and fb.style.set (js/style_arg.h, js/freebutton_device.c).
//
// A style is addressed by its ref (index + 1 in the table, 0 = none);
// style_hw_find() resolves a name. Script code (loop or button task, JS
// engine lock held). Only style_hw_set_* takes the LVGL lock, and only to
// store a value and a dirty bit: the LVGL task does the LVGL work.
//

#ifndef STYLE_BINDING_H
#define STYLE_BINDING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Number of usable named styles (0: the config has none, or none could be
// made at boot; the style methods and fb.style are then not created).
int style_hw_count(void);

// Ref of the style with this name (len bytes), 0 when there is none.
int style_hw_find(const char* name, size_t len);

// Does the style set bg_color or bg_opa (for lists only)?
bool style_hw_has_bg(int ref);

// fb.style.set: one value of a named style, as config.json has it (a string
// or an integer). The items that use the style are restyled by the LVGL
// task. Returns STYLE_HW_SET_OK or the reason nothing changed. Waits for
// the LVGL lock, which the LVGL task holds for a whole render (80-150 ms):
// see style_binding.cpp.
enum {
    STYLE_HW_SET_OK = 0,
    STYLE_HW_SET_UNKNOWN = -1,      // not a style property
    STYLE_HW_SET_NOT_IN_STYLE = -2, // the style does not set it in config
    STYLE_HW_SET_FIXED = -3,        // text_font / text_size
    STYLE_HW_SET_BAD_VALUE = -4,    // wrong type or out of range
    STYLE_HW_SET_NO_MEMORY = -5,    // the config values could not be kept
};
int style_hw_set_string(int ref, const char* prop, const char* value);
int style_hw_set_number(int ref, const char* prop, int32_t value);

#ifdef __cplusplus
}
#endif

#endif // STYLE_BINDING_H
