// Emulator shim: the firmware's LVGL display owner. One thread, so the LVGL
// lock is free; the screens are the virtual display's (ui_shim.cpp).
#pragma once
// The firmware header's own guard: a relative include of it is skipped.
#ifndef LVGLDISPLAY_H
#define LVGLDISPLAY_H
#endif
#include <stddef.h>
#include <lvgl.h>

namespace FreeButton {
class LvglDisplay {
public:
    void lock() const {}
    void unlock() const {}
    lv_obj_t* getMainScreen() const;
    // The key display with that index (DisplayManager), nullptr when none.
    lv_display_t* getSmallDisplay(int index) const;
    bool acquireSmallDisplayBuffer(int index) const { (void)index; return true; }
    void releaseSmallDisplayBuffer(int index) const { (void)index; }
    // The LVGL pool: nothing is drawn here, so it is always as after boot.
    void getLvglMemory(size_t& free, size_t& total) const;
};

LvglDisplay& getLvglDisplay();
} // namespace FreeButton
