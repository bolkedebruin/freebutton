//
// Style binding - C interface to the styles (config "styles",
// ui/ui_style.h) for the JS API: the style-list argument of the setStyle
// methods and fb.style.set (js/style_arg.h, js/freebutton_device.c).
//
// A style is addressed by its ref (index + 1 in the table, 0 = none), an
// item's styles by a list (four refs in a word, style_logic.h). Script
// code (loop or button task, JS engine lock held). Only style_hw_set_*
// takes the LVGL lock, and only to store a value and a dirty bit: the LVGL
// task does the LVGL work.
//

#ifndef STYLE_BINDING_H
#define STYLE_BINDING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// A "style" string as config.json has it ("big alarm": up to four names
// separated by spaces; a name given twice counts once, at its last place)
// -> *list. On an error *list is unchanged and *bad / *bad_len give the
// name it is about (a part of `text`).
enum {
    STYLE_HW_LIST_OK = 0,
    STYLE_HW_LIST_UNKNOWN = -1,  // no such style
    STYLE_HW_LIST_RESERVED = -2, // "button" / "displayitem" / "list": not for an item
    STYLE_HW_LIST_TOO_MANY = -3, // a fifth name
};
int style_hw_parse_list(const char* text, uint32_t* list, const char** bad, size_t* bad_len);

// The names of a list joined by one space (""  for none). `out` holds at
// least STYLE_HW_LIST_TEXT_MAX bytes. Returns the length.
#define STYLE_HW_LIST_TEXT_MAX 100
size_t style_hw_list_text(uint32_t list, char* out, size_t capacity);

// Ref of the style with this name (len bytes); 0 when there is none. A
// reserved name gives a negative number: -1 "button", -2 "displayitem",
// -3 "list" (-(kind + 1): the style of a kind, which no item names but
// fb.style.set can change).
int style_hw_find(const char* name, size_t len);

// fb.style.set: one value of a style, as config.json has it (a string or a
// number). `ref` is what style_hw_find gave (a negative one: the style of a
// kind). `prop` is a property name, or "title." / "unit." / "selected." /
// "icon." and one. The items that use the style (for a kind: every item of
// it) are restyled by the LVGL task. Returns STYLE_HW_SET_OK or the reason nothing changed. Waits for
// the LVGL lock, which the LVGL task holds for a whole render (80-150 ms):
// see style_binding.cpp.
enum {
    STYLE_HW_SET_OK = 0,
    STYLE_HW_SET_UNKNOWN = -1,      // not a style property
    STYLE_HW_SET_NO_PART = -2,      // the style has no such block (a kind: no such part)
    STYLE_HW_SET_FIXED = -3,        // font-family / font-size / line-height
    STYLE_HW_SET_BAD_VALUE = -4,    // wrong type or out of range
    STYLE_HW_SET_NO_MEMORY = -5,    // no heap for the copy of the config values, or no room in
                                    // the LVGL pool for one more property
};
int style_hw_set_string(int ref, const char* prop, const char* value);
int style_hw_set_number(int ref, const char* prop, double value);

#ifdef __cplusplus
}
#endif

#endif // STYLE_BINDING_H
