//
// LvglRoom: is there room in the LVGL pool to build a widget?
//
// Display items, the clock and lists build their LVGL objects when their page
// is shown and delete them when it goes (see MainPage.h). LVGL halts on a
// failed allocation (LV_USE_ASSERT_MALLOC, LV_ASSERT_HANDLER while(1)), so a
// widget checks first and skips building when the pool is short: the page
// then lacks that widget (logged) instead of the device hanging. The next
// render request tries again.
//
// The sizes are what one widget takes, with margin. Measured on btn_a00001: a
// box ~2 KB on average, a roller ~600 B. The clock is an estimate (no clock
// on that device). A long value can take more than the box size once its
// text is rendered; this check is a safety net, not a budget.
//
#ifndef FREEBUTTON_LVGLROOM_H
#define FREEBUTTON_LVGLROOM_H

#include <cstddef>

#include <lvgl.h>

namespace freebutton::ui {

// A display item box with caption, value and unit.
constexpr size_t kRoomForBox = 2560;
// The analog clock (scale, three hands, timer).
constexpr size_t kRoomForClock = 4096;
// A list roller.
constexpr size_t kRoomForList = 1024;

// LVGL lock held. Walks the pool once (cheap; only when a widget is built).
inline bool lvglHasRoom(const size_t bytes) {
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    return mon.free_size >= bytes;
}

} // namespace freebutton::ui

#endif // FREEBUTTON_LVGLROOM_H
