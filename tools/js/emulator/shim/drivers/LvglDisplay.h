// Emulator's LVGL display owner, in place of src/drivers/LvglDisplay.h.
//
// The firmware's file is welded to LovyanGFX, DMA and FreeRTOS; the emulator
// replaces it (tools/js/emulator/display.cpp) and keeps what the rest of the
// firmware and LVGL see: one LVGL display per panel with the firmware's size,
// colour format, render mode and buffer sizes, the default dark theme, the
// main screen, the fonts, and task(): pending renders, then LVGL's timers.
// The flush hands pixels to a frame buffer the host reads.
#pragma once
// The firmware header's own guard: a relative include of it is skipped.
#ifndef LVGLDISPLAY_H
#define LVGLDISPLAY_H
#endif
#include <stddef.h>
#include <lvgl.h>

namespace FreeButton {
class DisplayManager;

class LvglDisplay {
public:
    bool initialize(DisplayManager& displayManager);
    // One turn of the firmware's LVGL task.
    void task();
    // One thread: the LVGL lock is free.
    void lock() const {}
    void unlock() const {}
    lv_display_t* getMainDisplay() const;
    lv_obj_t* getMainScreen() const;
    // The key display with that index (DisplayManager), nullptr when none.
    lv_display_t* getSmallDisplay(int index) const;
    size_t getSmallDisplayCount() const;
    // The key displays share one draw buffer, as on the device.
    bool acquireSmallDisplayBuffer(int index) const;
    void releaseSmallDisplayBuffer(int index) const;
    bool isInitialized() const;
    // The LVGL pool, sampled by task() once a second.
    void getLvglMemory(size_t& freeBytes, size_t& totalBytes) const;
};

bool initializeLvgl(DisplayManager& displayManager);
LvglDisplay& getLvglDisplay();
} // namespace FreeButton
