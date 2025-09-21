#ifndef LVGLDISPLAY_H
#define LVGLDISPLAY_H

#include <lvgl.h>
#include <memory>
#include "DisplayManager.h"

namespace ButtonPlus {

class LvglDisplay {
private:
    lv_display_t* main_display_;
    std::unique_ptr<uint8_t[]> buffer1_;
    SemaphoreHandle_t lvgl_mutex_;
    bool initialized_;
    lv_obj_t* progress_bar_;
    lv_obj_t* progress_label_;

public:
    LvglDisplay();
    ~LvglDisplay();

    // Initialize LVGL with LovyanGFX integration
    bool initialize(DisplayManager& displayManager);

    // Progress bar functionality for startup
    void showProgressBar(const char* message = "Initializing...");
    void updateProgress(int percentage, const char* message = nullptr);
    void hideProgressBar();

    // LVGL task management
    void task();

    // Lock/unlock for thread safety
    void lock();
    void unlock();

    // Get display objects
    lv_display_t* getMainDisplay() const { return main_display_; }

    // Utility
    bool isInitialized() const { return initialized_; }

private:
    static void flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map);
    void setupProgressBar();
};

// Global LVGL display instance
extern std::unique_ptr<LvglDisplay> g_lvglDisplay;

// Global convenience functions
bool initializeLvgl(DisplayManager& displayManager);
LvglDisplay& getLvglDisplay();

} // namespace ButtonPlus

#endif // LVGLDISPLAY_H