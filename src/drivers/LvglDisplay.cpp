#include "LvglDisplay.h"
#include "../ui/wifi_qr_modal.h"
#include "../ui/ui_button.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace FreeButton {

static auto TAG = "LvglDisplay";

// Global instance
std::unique_ptr<LvglDisplay> g_lvglDisplay;

// Reference to display manager for flush callback
static DisplayManager* g_display_mgr = nullptr;

LvglDisplay::LvglDisplay()
    : main_display_(nullptr)
    , buffer1_(nullptr)
    , shared_small_buffer_(nullptr)
    , lvgl_mutex_(nullptr)
    , small_buffer_mutex_(nullptr)
    , initialized_(false)
    , progress_bar_(nullptr)
    , progress_label_(nullptr)
    , create_hello_button_pending_(false)
{
}

LvglDisplay::~LvglDisplay() {
    if (lvgl_mutex_) {
        vSemaphoreDelete(lvgl_mutex_);
    }
    if (small_buffer_mutex_) {
        vSemaphoreDelete(small_buffer_mutex_);
    }
}

bool LvglDisplay::initialize(DisplayManager& displayManager) {
    ESP_LOGI(TAG, "Initializing LVGL display integration");

    g_display_mgr = &displayManager;

    // Initialize LVGL
    lv_init();

    // Create mutex for thread safety
    lvgl_mutex_ = xSemaphoreCreateMutex();
    if (!lvgl_mutex_) {
        ESP_LOGE(TAG, "Failed to create LVGL mutex");
        return false;
    }

    // Allocate display buffer (20 lines)
    constexpr size_t buffer_size = 320 * 20 * sizeof(lv_color_t);
    buffer1_ = static_cast<uint8_t*>(heap_caps_aligned_alloc(32, buffer_size, MALLOC_CAP_DMA));

    if (!buffer1_) {
        ESP_LOGE(TAG, "Failed to allocate LVGL buffer");
        return false;
    }

    ESP_LOGI(TAG, "Allocated LVGL buffer: %zu bytes", buffer_size);

    // Get main display from display manager
    auto* main_gfx = displayManager.getMainDisplay();
    if (!main_gfx) {
        ESP_LOGE(TAG, "No main display available");
        return false;
    }

    // Create LVGL display
    main_display_ = lv_display_create(320, 240);
    if (!main_display_) {
        ESP_LOGE(TAG, "Failed to create LVGL display");
        return false;
    }

    // Set flush callback
    lv_display_set_flush_cb(main_display_, flush_cb);

    // Set display buffer (single buffer only)
    lv_display_set_buffers(main_display_, buffer1_, nullptr,
                          buffer_size, LV_DISPLAY_RENDER_MODE_PARTIAL);

    // Set default theme
    lv_theme_t* theme = lv_theme_default_init(main_display_,
                                             lv_palette_main(LV_PALETTE_BLUE),
                                             lv_palette_main(LV_PALETTE_RED),
                                             LV_THEME_DEFAULT_DARK,
                                             LV_FONT_DEFAULT);
    lv_display_set_theme(main_display_, theme);

    // Create and start LVGL tick timer
    constexpr esp_timer_create_args_t timer_args = {
        .callback = [](void* arg) { lv_tick_inc(1); },
        .name = "lvgl_tick"
    };

    esp_timer_handle_t timer;
    if (esp_timer_create(&timer_args, &timer) == ESP_OK) {
        esp_timer_start_periodic(timer, 1000); // 1ms tick
        ESP_LOGI(TAG, "LVGL tick timer started");
    } else {
        ESP_LOGE(TAG, "Failed to create LVGL tick timer");
        return false;
    }

    // Setup progress bar for startup
    setupProgressBar();

    // Initialize small displays
    initializeSmallDisplays(displayManager);

    initialized_ = true;
    ESP_LOGI(TAG, "LVGL display initialization completed successfully");

    return true;
}

void LvglDisplay::flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    if (!g_display_mgr) {
        lv_display_flush_ready(disp);
        return;
    }

    auto* main_gfx = g_display_mgr->getMainDisplay();
    if (!main_gfx) {
        lv_display_flush_ready(disp);
        return;
    }

    // Select main display before operations
    g_display_mgr->selectMainDisplay();

    // Calculate area dimensions
    const uint32_t width = area->x2 - area->x1 + 1;
    const uint32_t height = area->y2 - area->y1 + 1;

    // Start write operation
    main_gfx->startWrite();
    main_gfx->setAddrWindow(area->x1, area->y1, width, height);

    // Push pixels to display
    while (main_gfx->dmaBusy()) {
        vTaskDelay(1);
    }
    main_gfx->pushImageDMA(area->x1, area->y1, width, height, reinterpret_cast<uint16_t *>(px_map));

    // End write operation
    main_gfx->endWrite();

    // Deselect all displays after operation
    g_display_mgr->deselectAllDisplays();

    // Signal LVGL that flushing is complete
    lv_display_flush_ready(disp);
}

void LvglDisplay::setupProgressBar() {
    if (!main_display_) return;

    // Select main display and clear it first
    if (g_display_mgr) {
        g_display_mgr->selectMainDisplay();
        auto* main_gfx = g_display_mgr->getMainDisplay();
        if (main_gfx) {
            main_gfx->fillScreen(0x0000); // Clear to black
        }
        g_display_mgr->deselectAllDisplays();
    }

    // Create main screen
    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_scr_load(scr);

    // Create progress label (centered above progress bar)
    progress_label_ = lv_label_create(scr);
    lv_label_set_text(progress_label_, "Initializing...");
    lv_obj_set_style_text_color(progress_label_, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(progress_label_, &lv_font_montserrat_14, 0);
    lv_obj_align(progress_label_, LV_ALIGN_CENTER, 0, -30);

    // Create iPhone-style progress bar (thin, rounded, centered)
    progress_bar_ = lv_bar_create(scr);
    lv_obj_set_size(progress_bar_, 200, 3);
    lv_obj_align(progress_bar_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(progress_bar_, lv_color_hex(0x3a3a3c), LV_PART_MAIN);
    lv_obj_set_style_bg_color(progress_bar_, lv_color_hex(0xffffff), LV_PART_INDICATOR);
    lv_obj_set_style_radius(progress_bar_, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(progress_bar_, 2, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(progress_bar_, 0, LV_PART_MAIN);
    lv_bar_set_value(progress_bar_, 0, LV_ANIM_OFF);

    ESP_LOGI(TAG, "Progress bar setup completed");
}

void LvglDisplay::showProgressBar(const char* message) {
    if (!initialized_ || !progress_label_) return;

    lock();
    lv_label_set_text(progress_label_, message);
    lv_bar_set_value(progress_bar_, 0, LV_ANIM_OFF);
    unlock();
}

void LvglDisplay::updateProgress(int percentage, const char* message) {
    if (!initialized_ || !progress_bar_) return;

    lock();

    // Update progress bar
    lv_bar_set_value(progress_bar_, percentage, LV_ANIM_ON);

    // Update message if provided
    if (message && progress_label_) {
        lv_label_set_text(progress_label_, message);
    }


    unlock();

    ESP_LOGD(TAG, "Progress updated: %d%% - %s", percentage, message ? message : "");
}

void LvglDisplay::hideProgressBar() {
    if (!initialized_ || !progress_bar_) return;

    lock();

    // Create new main application screen
    lv_obj_t* main_scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(main_scr, lv_color_hex(0x000000), 0);

    // Create welcome label
    lv_obj_t* welcome_label = lv_label_create(main_scr);
    lv_label_set_text(welcome_label, "FreeButton Ready");
    lv_obj_set_style_text_color(welcome_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(welcome_label, &lv_font_montserrat_14, 0);
    lv_obj_center(welcome_label);

    // Load the new screen
    lv_scr_load_anim(main_scr, LV_SCR_LOAD_ANIM_FADE_IN, 500, 0, false);

    unlock();

    // Request creation of the Google Hello button (will be handled in LVGL task)
    requestGoogleHelloButton();

    ESP_LOGI(TAG, "Progress bar hidden, main UI loaded");
}

void LvglDisplay::task() {
    if (!initialized_) return;

    lock();

    // Handle pending UI creation requests
    if (create_hello_button_pending_) {
        create_hello_button_pending_ = false;
        unlock(); // Unlock before creating UI to avoid deadlocks
        createGoogleHelloButton();
        lock(); // Re-lock for the rest of the function
    }

    lv_timer_handler();

    // Process any pending WiFi QR modal operations
    wifi_qr_modal_process_pending();

    unlock();
}

void LvglDisplay::lock() {
    if (lvgl_mutex_) {
        xSemaphoreTake(lvgl_mutex_, portMAX_DELAY);
    }
}

void LvglDisplay::unlock() {
    if (lvgl_mutex_) {
        xSemaphoreGive(lvgl_mutex_);
    }
}

// Global convenience functions
bool initializeLvgl(DisplayManager& displayManager) {
    if (!g_lvglDisplay) {
        g_lvglDisplay = std::make_unique<LvglDisplay>();
    }

    return g_lvglDisplay->initialize(displayManager);
}

LvglDisplay& getLvglDisplay() {
    if (!g_lvglDisplay) {
        g_lvglDisplay = std::make_unique<LvglDisplay>();
    }

    return *g_lvglDisplay;
}

void LvglDisplay::initializeSmallDisplays(DisplayManager& displayManager) {
    ESP_LOGI(TAG, "Initializing small display LVGL integration");

    // Create shared buffer mutex
    small_buffer_mutex_ = xSemaphoreCreateMutex();
    if (!small_buffer_mutex_) {
        ESP_LOGE(TAG, "Failed to create small buffer mutex");
        return;
    }

    // Allocate shared buffer for small displays
    constexpr size_t small_buffer_size = 160 * 5 * sizeof(lv_color_t);
    shared_small_buffer_ = static_cast<uint8_t*>(
        heap_caps_aligned_alloc(32, small_buffer_size, MALLOC_CAP_DMA)
    );

    if (!shared_small_buffer_) {
        ESP_LOGE(TAG, "Failed to allocate shared small display buffer");
        return;
    }

    ESP_LOGI(TAG, "Allocated shared small display buffer: %zu bytes", small_buffer_size);

    // Create LVGL displays for each small display
    size_t smallDisplayCount = displayManager.getSmallDisplayCount();
    small_displays_.reserve(smallDisplayCount);

    for (size_t i = 0; i < smallDisplayCount; ++i) {
        auto* small_gfx = displayManager.getSmallDisplay(static_cast<int>(i));
        if (!small_gfx) continue;

        // Create LVGL display
        lv_display_t* small_disp = lv_display_create(160, 80);
        if (!small_disp) {
            ESP_LOGE(TAG, "Failed to create LVGL display for small display %zu", i);
            continue;
        }

        // Set flush callback
        lv_display_set_flush_cb(small_disp, flush_small_cb);

        // Store the LVGL display object
        small_displays_.push_back(small_disp);

        // Set user data to the index for easy lookup in flush callback
        lv_display_set_user_data(small_disp, reinterpret_cast<void*>(i));

        ESP_LOGI(TAG, "Small display %zu LVGL integration created", i);
    }

    ESP_LOGI(TAG, "Small display LVGL integration completed - %zu displays", small_displays_.size());

    // Clear all small displays to black background
    clearAllSmallDisplays();
}

void LvglDisplay::flush_small_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    if (!g_display_mgr || !g_lvglDisplay) {
        lv_display_flush_ready(disp);
        return;
    }

    // Get display index from user data
    int index = reinterpret_cast<intptr_t>(lv_display_get_user_data(disp));
    auto* small_gfx = g_display_mgr->getSmallDisplay(index);
    if (!small_gfx) {
        lv_display_flush_ready(disp);
        return;
    }

    // Get the proper connector and position from DisplayManager
    int connector, position;
    if (!g_display_mgr->getSmallDisplayInfo(index, connector, position)) {
        ESP_LOGE(TAG, "Could not get connector/position info for display %d", index);
        lv_display_flush_ready(disp);
        return;
    }

    // Select the specific small display using DisplayManager's method
    g_display_mgr->selectSmallDisplay(connector, position);

    // Calculate area dimensions
    const uint32_t width = area->x2 - area->x1 + 1;
    const uint32_t height = area->y2 - area->y1 + 1;

    // Start write operation
    small_gfx->startWrite();
    small_gfx->setAddrWindow(area->x1, area->y1, width, height);

    // Push pixels to display
    small_gfx->writePixels(reinterpret_cast<uint16_t*>(px_map), width * height);

    // End write operation
    small_gfx->endWrite();

    // Deselect all displays after operation
    g_display_mgr->deselectAllDisplays();

    // Signal LVGL that flushing is complete
    lv_display_flush_ready(disp);
}

lv_display_t* LvglDisplay::getSmallDisplay(int index) const {
    if (index >= 0 && index < static_cast<int>(small_displays_.size())) {
        return small_displays_[index];
    }
    return nullptr;
}

bool LvglDisplay::acquireSmallDisplayBuffer(int index) const {
    if (!small_buffer_mutex_ || !shared_small_buffer_) {
        return false;
    }

    auto* disp = getSmallDisplay(index);
    if (!disp) {
        return false;
    }

    if (xSemaphoreTake(small_buffer_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to acquire small buffer mutex for display %d", index);
        return false;
    }

    constexpr size_t small_buffer_size = 160 * 5 * sizeof(lv_color_t);
    lv_display_set_buffers(disp, shared_small_buffer_, nullptr,
                          small_buffer_size, LV_DISPLAY_RENDER_MODE_PARTIAL);

    // Enable invalidation now that buffer is properly set
    lv_display_enable_invalidation(disp, true);

    ESP_LOGD(TAG, "Small display %d buffer acquired", index);
    return true;
}

void LvglDisplay::releaseSmallDisplayBuffer() const {
    if (small_buffer_mutex_) {
        xSemaphoreGive(small_buffer_mutex_);
        ESP_LOGD(TAG, "Small display buffer released");
    }
}

void LvglDisplay::clearAllSmallDisplays() const {
    if (!g_display_mgr) {
        ESP_LOGW(TAG, "Display manager not available for clearing small displays");
        return;
    }

    ESP_LOGI(TAG, "Clearing all small displays to black");

    for (size_t i = 0; i < small_displays_.size(); ++i) {
        auto* small_gfx = g_display_mgr->getSmallDisplay(static_cast<int>(i));
        if (!small_gfx) continue;

        // Get the proper connector and position from DisplayManager
        int connector, position;
        if (!g_display_mgr->getSmallDisplayInfo(static_cast<int>(i), connector, position)) {
            ESP_LOGW(TAG, "Could not get connector/position info for display %zu", i);
            continue;
        }

        ESP_LOGD(TAG, "Clearing small display %zu (connector %d, position %d)", i, connector, position);

        // Use DisplayManager's selectSmallDisplay method
        g_display_mgr->selectSmallDisplay(connector, position);
        //vTaskDelay(pdMS_TO_TICKS(10)); // Small delay for CS setup

        // Clear to black
        small_gfx->fillScreen(0x0000);

        //vTaskDelay(pdMS_TO_TICKS(10)); // Small delay before deselect
        g_display_mgr->deselectAllDisplays();

        ESP_LOGI(TAG, "Cleared small display %zu (connector %d, position %d)", i, connector, position);
    }

    ESP_LOGI(TAG, "All small displays cleared to black");
}

void LvglDisplay::requestGoogleHelloButton() {
    ESP_LOGI(TAG, "Requesting Google Hello button creation");
    create_hello_button_pending_ = true;
}

void LvglDisplay::createGoogleHelloButton() {
    // Get the first small display
    auto* first_small_disp = getSmallDisplay(0);
    if (!first_small_disp) {
        ESP_LOGW(TAG, "No first small display available for Hello button");
        return;
    }

    // Acquire buffer for the first small display
    if (!acquireSmallDisplayBuffer(0)) {
        ESP_LOGW(TAG, "Failed to acquire buffer for first small display");
        return;
    }

    // Save current default display and temporarily set to small display
    lv_display_t* original_default_disp = lv_display_get_default();
    lv_display_set_default(first_small_disp);

    // Initialize button styles
    ui_button_styles();

    // Create a screen for the small display
    lv_obj_t* small_scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(small_scr, lv_color_hex(0x000000), 0);
    lv_scr_load_anim(small_scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);

    // Create the button using our ui_button
    lv_obj_t* hello_button = ui_button_create();
    if (!hello_button) {
        ESP_LOGE(TAG, "Failed to create Hello button");
        releaseSmallDisplayBuffer();
        // Restore original default display before returning
        lv_display_set_default(original_default_disp);
        return;
    }

    // Set the parent to our small screen
    lv_obj_set_parent(hello_button, small_scr);

    // Position the button to fit nicely on the 160x80 display
    lv_obj_set_size(hello_button, 150, 70);
    lv_obj_center(hello_button);

    // Create a colored "Hello" text using Google colors
    // We'll use different colors for each letter: H-e-l-l-o
    // Google colors: Blue #4285F4, Red #EA4335, Yellow #FBBC05, Green #34A853

    // For now, let's set the title and main text
    ui_button_set_title(hello_button, "");  // No title to maximize space for Hello

    // Create individual labels for each letter with Google colors
    const char letters[] = {'H', 'e', 'l', 'l', 'o'};
    const uint32_t google_colors[] = {0x4285F4, 0xEA4335, 0xFBBC05, 0x34A853, 0x4285F4}; // Blue, Red, Yellow, Green, Blue

    lv_obj_t* hello_container = lv_obj_create(hello_button);
    lv_obj_set_style_bg_opa(hello_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hello_container, 0, 0);
    lv_obj_set_style_pad_all(hello_container, 0, 0);
    lv_obj_set_size(hello_container, lv_pct(100), lv_pct(100));
    lv_obj_set_layout(hello_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(hello_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hello_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_center(hello_container);

    // Create labels for each letter
    for (int i = 0; i < 5; i++) {
        lv_obj_t* letter_label = lv_label_create(hello_container);
        char letter_str[2] = {letters[i], '\0'};
        lv_label_set_text(letter_label, letter_str);
        lv_obj_set_style_text_color(letter_label, lv_color_hex(google_colors[i]), 0);
        lv_obj_set_style_text_font(letter_label, &lv_font_montserrat_14, 0);
    }

    // Force refresh the small display
    lv_obj_invalidate(small_scr);

    // Release the buffer after setting up the UI
    releaseSmallDisplayBuffer();

    // Restore original default display to prevent affecting other UI elements (like modals)
    lv_display_set_default(original_default_disp);

    ESP_LOGI(TAG, "Google-colored Hello button created on first small display");
}

} // namespace FreeButton