//
// Style binding - see style_binding.h.
//

#include "style_binding.h"

#include "drivers/LvglDisplay.h"
#include "ui/PendingRender.h"

extern "C" {
#include "ui/ui_style.h"
}

namespace {

uint8_t toRef(const int ref) {
    return ref >= 1 && ref <= STYLE_NAMED_MAX ? static_cast<uint8_t>(ref) : 0;
}

int toResult(const style_prop_status_t status) {
    switch (status) {
        case STYLE_PROP_OK: return STYLE_HW_SET_OK;
        case STYLE_PROP_UNKNOWN: return STYLE_HW_SET_UNKNOWN;
        case STYLE_PROP_NOT_IN_STYLE: return STYLE_HW_SET_NOT_IN_STYLE;
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

int style_hw_count(void) {
    return static_cast<int>(ui_style_named_count());
}

int style_hw_find(const char* name, const size_t len) {
    return name != nullptr ? ui_style_named_find(name, len) : 0;
}

bool style_hw_has_bg(const int ref) {
    // Which properties a style has does not change after config load.
    const style_props_t* props = ui_style_named_props(toRef(ref));
    return props != nullptr && (props->has & (STYLE_HAS_BG_COLOR | STYLE_HAS_BG_OPA)) != 0;
}

int style_hw_set_string(const int ref, const char* prop, const char* value) {
    return setValue([&] { return ui_style_named_set_string(toRef(ref), prop, value); });
}

int style_hw_set_number(const int ref, const char* prop, const int32_t value) {
    return setValue([&] { return ui_style_named_set_number(toRef(ref), prop, value); });
}

} // extern "C"
