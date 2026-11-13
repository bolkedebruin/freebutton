#ifndef LVGLDISPLAY_H
#define LVGLDISPLAY_H

#include <lvgl.h>
#include <atomic>
#include <memory>
#include <vector>
#include "DisplayManager.h"

namespace FreeButton {

// Mini-panel self-heal (bug-016). Nothing else ever re-initialises a mini
// panel, so one corrupted SPI/CS transfer would stay on screen indefinitely.
// Every kSmallPanelHealIntervalMs, and soon after any chip-select write
// failure, each mini panel gets its registers re-sent (no SWRESET, no clear)
// and its active screen invalidated so LVGL repaints it. The timer runs every
// kSmallPanelHealStepMs and heals at most one panel per run.
inline constexpr uint32_t kSmallPanelHealIntervalMs = 10u * 60u * 1000u;
inline constexpr uint32_t kSmallPanelHealStepMs = 1000u;

class LvglDisplay {
private:
    lv_display_t* main_display_;
    uint8_t* buffer1_;
    uint8_t* buffer2_;
    std::vector<lv_display_t*> small_displays_;
    uint8_t* shared_small_buffer_;
    uint8_t* shared_small_buffer2_ = nullptr;
    SemaphoreHandle_t lvgl_mutex_;
    SemaphoreHandle_t small_buffer_mutex_;
    bool initialized_;
    lv_obj_t* main_screen_;

    // LVGL pool usage, sampled once a second by task() (lock held). Read from
    // any task without the lock via getLvglMemory().
    uint32_t mem_sample_tick_ = 0;
    std::atomic<size_t> lvgl_mem_free_{0};
    std::atomic<size_t> lvgl_mem_total_{0};

    // Self-heal state. Only touched from the heal lv_timer callback, which
    // runs in the LVGL task inside lv_timer_handler() with lvgl_mutex_ held.
    lv_timer_t* heal_timer_;
    size_t heal_next_index_;          // round-robin position in small_displays_
    size_t heal_pending_;             // panels left to heal in the current sweep
    uint32_t heal_last_sweep_tick_;   // lv_tick_get() when the last sweep ended
    uint32_t heal_seen_cs_failures_;  // DisplayManager CS failure count last seen

public:
    LvglDisplay();
    ~LvglDisplay();

    // Initialize LVGL with LovyanGFX integration
    bool initialize(DisplayManager& displayManager);

    // LVGL task management
    void task();

    // Lock/unlock for thread safety
    void lock() const;
    void unlock() const;

    // Get display objects
    lv_display_t* getMainDisplay() const { return main_display_; }
    lv_obj_t* getMainScreen() const { return main_screen_; }
    lv_display_t* getSmallDisplay(int index) const;
    size_t getSmallDisplayCount() const { return small_displays_.size(); }

    // Small display buffer management
    bool acquireSmallDisplayBuffer(int index) const;
    void releaseSmallDisplayBuffer(int index) const;
    void clearAllSmallDisplays() const;

    // Utility
    bool isInitialized() const { return initialized_; }

    // Screenshot (debug, POST /api/screenshot): from any task. The LVGL task
    // then repaints every display once and writes what it flushes to
    // LittleFS /screenshot-<main|miniN>.rgb565: an 8-byte header ("FBSS",
    // uint16 LE width, uint16 LE height) and RGB565 big-endian pixels, row by
    // row (tools/scripts/screenshot.py turns them into PNGs).
    static void requestScreenshot();

    // Benchmark (debug, POST /api/bench?n=N): from any task. The LVGL task
    // then repaints every display N times and records per display the
    // average/best time, DMA wait, bands and glyphs rasterized.
    // takeBenchResult() hands the newest unread result over once (JSON with a
    // "seq" that grows per run; the caller free()s it), else nullptr; *seq is
    // the newest run's number either way.
    static void requestBench(int repaints);
    static char* takeBenchResult(uint32_t* seq);

    // Last LVGL pool snapshot (free/total bytes). Safe from any task: it
    // never touches the pool, unlike lv_mem_monitor().
    void getLvglMemory(size_t& freeBytes, size_t& totalBytes) const;

private:
    void captureScreens();
    void runBench(int n);
    static void flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map);
    static void flush_small_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map);
    void initializeSmallDisplays(DisplayManager& displayManager);

    // Self-heal (LVGL task, lvgl_mutex_ already held: do not call lock()).
    static void heal_timer_cb(lv_timer_t* timer);
    void healStep();
    bool healSmallDisplay(size_t index);
};

// Global LVGL display instance
extern std::unique_ptr<LvglDisplay> g_lvglDisplay;

// Global convenience functions
bool initializeLvgl(DisplayManager& displayManager);
LvglDisplay& getLvglDisplay();

} // namespace FreeButton

#endif // LVGLDISPLAY_H