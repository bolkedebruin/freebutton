#include "LvglDisplay.h"
#include "../ui/wifi_qr_modal.h"
#include "../ui/ota_modal.h"
#include "../ui/PendingRender.h"
#include "../ui/ui_button.h"
#include "../core/FileSystem.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "ui/fonts/fonts.h"

#define MAIN_DISPLAY_WIDTH 320
#define MAIN_DISPLAY_HEIGHT 240
#define MAIN_DISPLAY_BUFFER_SIZE 20

#define SMALL_DISPLAY_WIDTH 160
#define SMALL_DISPLAY_HEIGHT 80
#define SMALL_DISPLAY_BUFFER_SIZE 5

namespace FreeButton {

static auto TAG = "LvglDisplay";

// Global instance
std::unique_ptr<LvglDisplay> g_lvglDisplay;

// Reference to display manager for flush callback
static DisplayManager* g_display_mgr = nullptr;

LvglDisplay::LvglDisplay()
    : main_display_(nullptr)
      , buffer1_(nullptr)
      , buffer2_(nullptr)
      , shared_small_buffer_(nullptr)
      , lvgl_mutex_(nullptr)
      , small_buffer_mutex_(nullptr)
      , initialized_(false)
      , main_screen_(nullptr)
      , create_hello_button_pending_(false)
      , heal_timer_(nullptr)
      , heal_next_index_(0)
      , heal_pending_(0)
      , heal_last_sweep_tick_(0)
      , heal_seen_cs_failures_(0) {
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

    // Allocate display buffer
    constexpr size_t buffer_size = MAIN_DISPLAY_WIDTH * MAIN_DISPLAY_BUFFER_SIZE * sizeof(lv_color_t);
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
    main_display_ = lv_display_create(MAIN_DISPLAY_WIDTH, MAIN_DISPLAY_HEIGHT);
    if (!main_display_) {
        ESP_LOGE(TAG, "Failed to create LVGL display");
        return false;
    }

    // The panels take RGB565 big-endian; the flush sends the buffer as-is
    // (LovyanGFX treats uint16_t* as pre-swapped). LVGL 9 dropped
    // LV_COLOR_16_SWAP, so render byte-swapped directly. Without this every
    // colour is byte-swapped on the wire (green 0x07E0 -> 0xE007, purple).
    lv_display_set_color_format(main_display_, LV_COLOR_FORMAT_RGB565_SWAPPED);

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

    // Initialize small displays
    initializeSmallDisplays(displayManager);

    // Create main screen
    if (g_display_mgr) {
        if (g_display_mgr->selectMainDisplay()) {
            auto* main_gfx = g_display_mgr->getMainDisplay();
            if (main_gfx) {
                main_gfx->fillScreen(0x0000); // Clear to black
            }
        }
        g_display_mgr->deselectAllDisplays();
    }

    main_screen_ = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(main_screen_, lv_color_hex(0x000000), 0);
    lv_scr_load(main_screen_);

    // Initialize LVGL filesystem interface
    lv_fs_if_init();

    ESP_LOGI(TAG, "LVGL filesystem interface initialized");

    init_fonts();

    // Mini-panel self-heal. Created here, in setup(), before the LVGL task
    // starts, so no lock is needed yet. The callback later runs inside
    // lv_timer_handler() in the LVGL task, with lvgl_mutex_ held, in the same
    // context as the flush callbacks, so it never races a flush on SPI or CS.
    // heal_seen_cs_failures_ starts at 0, so CS failures during boot trigger
    // a heal on the first run.
    heal_last_sweep_tick_ = lv_tick_get();
    heal_timer_ = lv_timer_create(heal_timer_cb, kSmallPanelHealStepMs, this);
    if (!heal_timer_) {
        ESP_LOGW(TAG, "Failed to create mini display self-heal timer");
    }

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

    // Select main display before operations. If a CS write failed, the CS
    // state is unknown: send nothing, so pixels cannot land on another panel.
    // The self-heal timer sees the failure count change and repaints.
    if (!g_display_mgr->selectMainDisplay()) {
        g_display_mgr->deselectAllDisplays();
        lv_display_flush_ready(disp);
        return;
    }

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

    // Text that MQTT / JS tasks stored since the last tick (display item
    // values, button labels). Rendered here, under the lock this task already
    // holds, so those tasks never wait for the LVGL lock; lv_timer_handler()
    // then draws it in this same tick.
    freebutton::ui::applyPendingRenders();

    lv_timer_handler();

    // Process any pending WiFi QR modal operations
    wifi_qr_modal_process_pending();

    // Process any pending OTA modal operations
    ota_modal_process_pending();

    unlock();
}

void LvglDisplay::lock() const {
    if (lvgl_mutex_) {
        xSemaphoreTake(lvgl_mutex_, portMAX_DELAY);
    }
}

void LvglDisplay::unlock() const {
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
    constexpr size_t small_buffer_size = SMALL_DISPLAY_WIDTH * SMALL_DISPLAY_BUFFER_SIZE * sizeof(lv_color_t);
    shared_small_buffer_ = static_cast<uint8_t*>(
        heap_caps_aligned_alloc(32, small_buffer_size, MALLOC_CAP_DMA)
    );

    if (!shared_small_buffer_) {
        ESP_LOGE(TAG, "Failed to allocate shared small display buffer");
        return;
    }

    ESP_LOGD(TAG, "Allocated shared small display buffer: %zu bytes", small_buffer_size);

    // Create LVGL displays for each small display
    size_t smallDisplayCount = displayManager.getSmallDisplayCount();
    small_displays_.reserve(smallDisplayCount);

    for (size_t i = 0; i < smallDisplayCount; ++i) {
        auto* small_gfx = displayManager.getSmallDisplay(static_cast<int>(i));
        if (!small_gfx) continue;

        // Create LVGL display
        lv_display_t* small_disp = lv_display_create(SMALL_DISPLAY_WIDTH, SMALL_DISPLAY_HEIGHT);
        if (!small_disp) {
            ESP_LOGE(TAG, "Failed to create LVGL display for small display %zu", i);
            continue;
        }

        lv_display_set_color_format(small_disp, LV_COLOR_FORMAT_RGB565_SWAPPED);  // see main display
        lv_display_set_flush_cb(small_disp, flush_small_cb);
        small_displays_.push_back(small_disp);

        // Set user data to the index for easy lookup in flush callback
        lv_display_set_user_data(small_disp, reinterpret_cast<void*>(i));

        ESP_LOGD(TAG, "Small display %zu LVGL initialized", i);
    }

    ESP_LOGI(TAG, "Small display LVGL initalization completed - %zu displays", small_displays_.size());

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

    // Select the specific small display using DisplayManager's method. On a
    // CS write failure send nothing (see flush_cb); the self-heal repaints.
    if (!g_display_mgr->selectSmallDisplay(connector, position)) {
        g_display_mgr->deselectAllDisplays();
        lv_display_flush_ready(disp);
        return;
    }

    // Calculate area dimensions
    const uint32_t width = area->x2 - area->x1 + 1;
    const uint32_t height = area->y2 - area->y1 + 1;

    small_gfx->startWrite();
    small_gfx->setAddrWindow(area->x1, area->y1, width, height);

    // Push pixels to display - NO DMA for small displays for now
    small_gfx->writePixels(reinterpret_cast<uint16_t*>(px_map), width * height);
    small_gfx->endWrite();

    g_display_mgr->deselectAllDisplays();

    lv_display_flush_ready(disp);
}

// Runs inside lv_timer_handler() in the LVGL task, which already holds
// lvgl_mutex_ (see task()). Do not call lock() here: the mutex is not
// recursive. Same context as the flush callbacks, so SPI and CS use stay
// serialised with flushing.
void LvglDisplay::heal_timer_cb(lv_timer_t* timer) {
    auto* self = static_cast<LvglDisplay*>(lv_timer_get_user_data(timer));
    if (self) {
        self->healStep();
    }
}

void LvglDisplay::healStep() {
    if (!g_display_mgr) {
        return;
    }

    const size_t count = small_displays_.size();
    const uint32_t csFailures = g_display_mgr->getCsWriteFailureCount();

    if (csFailures != heal_seen_cs_failures_) {
        // A CS write failed: a panel may have been left selected and taken in
        // traffic meant for another one, and flushes were skipped. Heal every
        // mini panel (continuing the round-robin, so a steady stream of
        // failures cannot starve the later panels) and repaint the main screen.
        ESP_LOGW(TAG, "CS write failures %lu -> %lu, scheduling display self-heal",
                 static_cast<unsigned long>(heal_seen_cs_failures_),
                 static_cast<unsigned long>(csFailures));
        heal_seen_cs_failures_ = csFailures;
        heal_pending_ = count;
        if (main_display_) {
            if (lv_obj_t* scr = lv_display_get_screen_active(main_display_)) {
                lv_obj_invalidate(scr);
            }
        }
    } else if (heal_pending_ == 0 &&
               lv_tick_elaps(heal_last_sweep_tick_) >= kSmallPanelHealIntervalMs) {
        heal_pending_ = count;
    }

    if (heal_pending_ == 0 || count == 0) {
        return;
    }

    if (heal_next_index_ >= count) {
        heal_next_index_ = 0;
    }
    healSmallDisplay(heal_next_index_);
    heal_next_index_ = (heal_next_index_ + 1) % count;

    if (--heal_pending_ == 0) {
        heal_last_sweep_tick_ = lv_tick_get();
    }
}

bool LvglDisplay::healSmallDisplay(const size_t index) {
    lv_display_t* disp = getSmallDisplay(static_cast<int>(index));
    if (!disp) {
        return false;
    }

    // Same index lookup as flush_small_cb.
    const int dmIndex = static_cast<int>(reinterpret_cast<intptr_t>(lv_display_get_user_data(disp)));
    auto* small_gfx = g_display_mgr->getSmallDisplay(dmIndex);
    int connector, position;
    if (!small_gfx || !g_display_mgr->getSmallDisplayInfo(dmIndex, connector, position)) {
        return false;
    }

    if (!g_display_mgr->selectSmallDisplay(connector, position)) {
        // The failure count went up; healStep() schedules another sweep.
        g_display_mgr->deselectAllDisplays();
        ESP_LOGW(TAG, "Self-heal: CS select failed for small display %d", dmIndex);
        return false;
    }

    // Re-send the register setup without SWRESET or clear: nothing visible
    // changes on a healthy panel.
    small_gfx->refreshPanelRegisters();

    // A mini display without a Button never got an LVGL draw buffer (see
    // acquireSmallDisplayBuffer). Invalidating it would make LVGL render with
    // no buffer, so restore its boot state (black) directly instead.
    const bool hasLvglBuffer = lv_display_get_buf_active(disp) != nullptr;
    if (!hasLvglBuffer) {
        small_gfx->fillScreen(0x0000);
    }

    g_display_mgr->deselectAllDisplays();

    if (hasLvglBuffer) {
        // Full repaint on this display's next refresh, which also overwrites
        // any garbage in the panel's frame memory.
        if (lv_obj_t* scr = lv_display_get_screen_active(disp)) {
            lv_obj_invalidate(scr);
        }
    }

    ESP_LOGD(TAG, "Self-heal: refreshed small display %d (connector %d, position %d)",
             dmIndex, connector, position);
    return true;
}

lv_display_t* LvglDisplay::getSmallDisplay(const int index) const {
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

    constexpr size_t small_buffer_size = SMALL_DISPLAY_WIDTH * SMALL_DISPLAY_BUFFER_SIZE * sizeof(lv_color_t);
    lv_display_set_buffers(disp, shared_small_buffer_, nullptr,
                          small_buffer_size, LV_DISPLAY_RENDER_MODE_PARTIAL);

    // Enable invalidation now that buffer is properly set
    lv_display_enable_invalidation(disp, true);

    ESP_LOGD(TAG, "Small display %d buffer acquired", index);
    return true;
}

void LvglDisplay::releaseSmallDisplayBuffer(int index) const {
    if (small_buffer_mutex_) {
        auto* disp = getSmallDisplay(index);
        if (!disp) {
            ESP_LOGW(TAG, "No small display found for index %d during buffer release", index);
        } else {
            lv_display_enable_invalidation(disp, false);
        }
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
        if (!g_display_mgr->selectSmallDisplay(connector, position)) {
            ESP_LOGW(TAG, "CS select failed, not clearing small display %zu", i);
            g_display_mgr->deselectAllDisplays();
            continue;
        }

        // Clear to black
        small_gfx->fillScreen(0x0000);

        //vTaskDelay(pdMS_TO_TICKS(10)); // Small delay before deselect
        g_display_mgr->deselectAllDisplays();

        ESP_LOGI(TAG, "Cleared small display %zu (connector %d, position %d)", i, connector, position);
    }

    ESP_LOGI(TAG, "All small displays cleared");
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
    lv_obj_t* hello_button = ui_button_create(small_scr);
    if (!hello_button) {
        ESP_LOGE(TAG, "Failed to create Hello button");
        releaseSmallDisplayBuffer(1);
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
    releaseSmallDisplayBuffer(1);

    // Restore original default display to prevent affecting other UI elements (like modals)
    lv_display_set_default(original_default_disp);

    ESP_LOGI(TAG, "Google-colored Hello button created on first small display");
}

} // namespace FreeButton