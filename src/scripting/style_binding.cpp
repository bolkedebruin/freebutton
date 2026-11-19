//
// Style binding - see style_binding.h.
//

#include "style_binding.h"

#include "components/StyleRef.h"
#include "drivers/LvglDisplay.h"
#include "ui/PendingRender.h"

extern "C" {
#include "ui/ui_style.h"
}

static_assert(STYLE_HW_LIST_TEXT_MAX >= UI_STYLE_LIST_TEXT_MAX, "a list's names fit the script side's buffer");

namespace {

// A negative ref of style_hw_find: the kind.
style_kind_t toKind(const int ref) {
    return static_cast<style_kind_t>(-ref - 1);
}

int toResult(const style_prop_status_t status) {
    switch (status) {
        case STYLE_PROP_OK: return STYLE_HW_SET_OK;
        case STYLE_PROP_UNKNOWN: return STYLE_HW_SET_UNKNOWN;
        case STYLE_PROP_NO_PART: return STYLE_HW_SET_NO_PART;
        case STYLE_PROP_FIXED: return STYLE_HW_SET_FIXED;
        case STYLE_PROP_NO_MEMORY: return STYLE_HW_SET_NO_MEMORY;
        default: return STYLE_HW_SET_BAD_VALUE;
    }
}

// The LVGL lock for the table's values and its dirty bit only. The render
// request is made before the lock is given back, so the LVGL task's next
// walk is the one that sees the dirty bit.
//
// The wait is the cost of fb.style.set: the LVGL task holds the lock for a
// whole render (80-150 ms for a full repaint, measured), so each call can
// block the calling task (the loop task, or the button task for a key
// handler) that long, and several calls in one handler add up, each
// waiting for the render the one before it caused. Known and accepted
// (docs/STYLE.md says so: one property per event, or swap styles with
// setStyle, which stores a byte and does not wait). Not to be "fixed" with
// a lock-free write: the LVGL task reads these values while it fills the
// lv_style_t.
template <typename Set>
int setValue(Set&& set) {
    auto& display = FreeButton::getLvglDisplay();
    display.lock();
    const style_prop_status_t status = set();
    if (status == STYLE_PROP_OK) {
        freebutton::ui::requestPendingRender();
    }
    display.unlock();
    return toResult(status);
}

} // namespace

extern "C" {

int style_hw_parse_list(const char* text, uint32_t* list, const char** bad, size_t* bad_len) {
    const char* cursor = text;
    const char* name = nullptr;
    size_t len = 0;
    style_list_t next = 0;
    while (style_logic_list_next(&cursor, &name, &len)) {
        int result = STYLE_HW_LIST_OK;
        const uint8_t ref = ui_style_find(name, len);
        if (style_logic_kind(name, len) >= 0) {
            result = STYLE_HW_LIST_RESERVED;
        } else if (ref == 0) {
            result = STYLE_HW_LIST_UNKNOWN;
        } else if (!style_logic_list_add(&next, ref)) {
            result = STYLE_HW_LIST_TOO_MANY;
        }
        if (result != STYLE_HW_LIST_OK) {
            *bad = name;
            *bad_len = len;
            return result;
        }
    }
    *list = next;
    return STYLE_HW_LIST_OK;
}

size_t style_hw_list_text(const uint32_t list, char* out, const size_t capacity) {
    return ui_style_list_text(list, out, capacity);
}

int style_hw_find(const char* name, const size_t len) {
    if (name == nullptr) {
        return 0;
    }
    const int kind = style_logic_kind(name, len);
    return kind >= 0 ? -(kind + 1) : ui_style_find(name, len);
}

int style_hw_set_string(const int ref, const char* prop, const char* value) {
    style_part_t part = STYLE_PART_MAIN;
    const char* name = style_logic_split_prop(prop, &part);
    return setValue([&] {
        return ref < 0 ? ui_style_kind_set_string(toKind(ref), part, name, value)
                       : ui_style_set_string(freebutton::styleref::toRef(ref), part, name, value);
    });
}

int style_hw_set_number(const int ref, const char* prop, const double value) {
    style_part_t part = STYLE_PART_MAIN;
    const char* name = style_logic_split_prop(prop, &part);
    // An integer, or hundredths, as the property takes it; a property that
    // takes no number gets 0, and style_logic says what is wrong with it.
    // A number that is none for a style (14.5 for an integer, a third
    // decimal, NaN) goes on as STYLE_NUMBER_INVALID: the property and the
    // block are checked before the value, so "font-size", 14.5 is "cannot
    // be changed by a script" and not "not a valid value".
    const int scale = style_logic_prop_scale(name);
    int32_t number = 0;
    if (scale != 0 && !freebutton::styleref::scaled(value, scale, number)) {
        number = STYLE_NUMBER_INVALID;
    }
    return setValue([&] {
        return ref < 0 ? ui_style_kind_set_number(toKind(ref), part, name, number)
                       : ui_style_set_number(freebutton::styleref::toRef(ref), part, name, number);
    });
}

} // extern "C"
