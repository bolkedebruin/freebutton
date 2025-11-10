#ifndef LVGLDISPLAY_H
#define LVGLDISPLAY_H

#include <lvgl.h>
#include <memory>
#include <vector>
#include "DisplayManager.h"

namespace FreeButton {

class LvglDisplay {
private:
    lv_display_t* main_display_;
    uint8_t* buffer1_;
    uint8_t* buffer2_;
    std::vector<lv_display_t*> small_displays_;
    uint8_t* shared_small_buffer_;
    SemaphoreHandle_t lvgl_mutex_;
    SemaphoreHandle_t small_buffer_mutex_;
    bool initialized_;
    lv_obj_t* progress_bar_;
    lv_obj_t* progress_label_;
    bool create_hello_button_pending_;

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
    lv_display_t* getSmallDisplay(int index) const;
    size_t getSmallDisplayCount() const { return small_displays_.size(); }

    // Small display buffer management
    bool acquireSmallDisplayBuffer(int index) const;
    void releaseSmallDisplayBuffer(int index) const;
    void clearAllSmallDisplays() const;

    // Utility
    bool isInitialized() const { return initialized_; }

    // Example UI functions
    void requestGoogleHelloButton();
    void createGoogleHelloButton();

private:
    static void flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map);
    static void flush_small_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map);
    void setupProgressBar();
    void initializeSmallDisplays(DisplayManager& displayManager);
};

// Global LVGL display instance
extern std::unique_ptr<LvglDisplay> g_lvglDisplay;

// Global convenience functions
bool initializeLvgl(DisplayManager& displayManager);
LvglDisplay& getLvglDisplay();

} // namespace FreeButton

#endif // LVGLDISPLAY_H