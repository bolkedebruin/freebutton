#include "esp_private/esp_clk.h"
#include "LvglDisplay.h"
#include "../ui/wifi_qr_modal.h"
#include "../ui/ota_modal.h"
#include "../ui/PendingRender.h"
#include "../core/FileSystem.h"
#include "BusAwareFileSystem.h"
#include <atomic>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "ui/fonts/fonts.h"
#include "ui/render_stats.h"

#define MAIN_DISPLAY_WIDTH 320
#define MAIN_DISPLAY_HEIGHT 240
// Draw buffers (bytes; LVGL renders RGB565, 2 bytes per pixel, so 19200 B
// = 30 rows of 320). Two buffers let LVGL render the next band while the
// previous one goes out by DMA. docs/PERFORMANCE.md has the measurements.
#ifndef MAIN_BUF_BYTES
#define MAIN_BUF_BYTES (MAIN_DISPLAY_WIDTH * 20 * 3)
#endif
#ifndef MAIN_BUF_COUNT
#define MAIN_BUF_COUNT 1
#endif

#define SMALL_DISPLAY_WIDTH 160
#define SMALL_DISPLAY_HEIGHT 80
// Shared by all mini displays: 9600 B = 30 rows of 160, so a mini repaints
// in 3 bands instead of 12 (2400 B): 22-28 ms instead of 50-71 ms on .81,
// for 7.2 KB of heap paid once (docs/PERFORMANCE.md). LVGL's work per band
// (object tree walk, glyph lookups, rasterizing a glyph again for every
// band it crosses) dominates the repaint, not the pixels.
#ifndef SMALL_BUF_BYTES
#define SMALL_BUF_BYTES (SMALL_DISPLAY_WIDTH * 30 * 2)
#endif
#ifndef SMALL_BUF_COUNT
#define SMALL_BUF_COUNT 1
#endif

namespace FreeButton {

static auto TAG = "LvglDisplay";

// Global instance
std::unique_ptr<LvglDisplay> g_lvglDisplay;

// Screenshot capture (requestScreenshot / captureScreens): set only inside
// captureScreens(), on the LVGL task, around one lv_refr_now() of s_capDisp;
// the flush callbacks (same task) append what they send.
static std::atomic<bool> s_screenshotRequested{false};
// Benchmark (requestBench / runBench): repaints per display, 0 = none. The
// result is a heap-allocated JSON text, handed over once by
// takeBenchResult() (any task, under s_benchMux): no RAM is held for it
// outside a run.
static std::atomic<int> s_benchRequested{0};
static portMUX_TYPE s_benchMux = portMUX_INITIALIZER_UNLOCKED;
static constexpr size_t kBenchJsonMax = 3072;
static char* s_benchJson = nullptr;
static uint32_t s_benchSeq = 0;
static lv_display_t* s_capDisp = nullptr;
static BusAwareFile s_capFile;
static bool s_capOk = false;
static constexpr size_t kCapHeader = 8;

static void captureArea(lv_display_t* disp, const lv_area_t* area, const uint8_t* px) {
    if (disp != s_capDisp || !s_capFile) return;
    const size_t w = static_cast<size_t>(lv_display_get_horizontal_resolution(disp));
    const size_t aw = static_cast<size_t>(area->x2 - area->x1 + 1);
    const size_t ah = static_cast<size_t>(area->y2 - area->y1 + 1);
    if (area->x1 == 0 && aw == w) {  // full-width band: one write
        if (!s_capFile.seek(kCapHeader + static_cast<size_t>(area->y1) * w * 2) ||
            s_capFile.write(px, aw * ah * 2) != aw * ah * 2) {
            s_capOk = false;
        }
        return;
    }
    for (size_t row = 0; row < ah; ++row) {
        const size_t off = kCapHeader + ((static_cast<size_t>(area->y1) + row) * w + area->x1) * 2;
        if (!s_capFile.seek(off) || s_capFile.write(px + row * aw * 2, aw * 2) != aw * 2) {
            s_capOk = false;
        }
    }
}

// Reference to display manager for flush callback
static DisplayManager* g_display_mgr = nullptr;

// ---------------------------------------------------------------------------
// Panel DMA flush, main and mini panels (LVGL task, LVGL lock held).
//
// flush_cb() / flush_small_cb() start a band by DMA and return; LVGL is told
// the band is done (lv_display_flush_ready) only once the DMA has finished:
// in that display's flush_wait_cb (before LVGL reuses the buffer for the next
// band) or when the bus is released. Between bands of the same display, its
// CS stays selected and its SPI transaction stays open: every CS change costs
// one I2C write per MCP, more than sending a band.
//
// LVGL does not wait for the last band of a refresh, so a panel can still
// hold the bus when lv_timer_handler() moves on. Everything else that uses
// SPI2, the chip selects or the shared mini draw buffer must therefore first
// call releasePanelBus() (bus, CS) or completePanelDma() (buffer only):
//   - flush_cb / flush_small_cb for another display:           release
//   - LV_EVENT_RENDER_START on any mini (shared buffer):        complete
//   - heal, clearAllSmallDisplays, end of a screenshot refresh: release
//   - LvglDisplay::unlock():                                    release
// Releasing in unlock() means the bus is never held while the LVGL lock is
// free, and is always released by the task that took it. The Arduino SPI bus
// lock that LovyanGFX takes in startWrite() is a non-recursive mutex: taking
// it again from this task while a panel holds it would deadlock. That is
// also why the panels must keep bus_shared = false: with it, BusAwareFile
// (font reads while rendering) would take the same lock.
// ---------------------------------------------------------------------------
struct PanelBusHold {
    lv_display_t* disp = nullptr;          // display whose CS + SPI transaction are held
    lgfx::LGFX_Device* gfx = nullptr;
    bool dmaPending = false;               // a band may still be going out by DMA
    lv_area_t area{};                      // that band (for the screenshot capture)
    const uint8_t* px = nullptr;
};
static PanelBusHold s_panelHold;

// Waits for the pending band's DMA, then completes it for LVGL.
// Render statistics (POST /api/bench): DMA bands started and the time spent
// waiting for one to finish. LVGL task only.
static uint32_t s_statFlushes = 0;
static int64_t s_statDmaWaitUs = 0;

static void completePanelDma() {
    if (!s_panelHold.dmaPending) return;
    const int64_t w0 = esp_timer_get_time();
    s_panelHold.gfx->waitDMA();
    s_statDmaWaitUs += esp_timer_get_time() - w0;
    s_panelHold.dmaPending = false;
    captureArea(s_panelHold.disp, &s_panelHold.area, s_panelHold.px);
    lv_display_flush_ready(s_panelHold.disp);
}

// Completes any pending band, ends the held transaction and deselects the CS.
static void releasePanelBus() {
    if (!s_panelHold.disp) return;
    completePanelDma();
    s_panelHold.gfx->endWrite();
    g_display_mgr->deselectAllDisplays();
    s_panelHold = PanelBusHold{};
}

// Makes `disp` the bus holder: releases another holder, selects the panel
// (select() returns false on a CS write failure) and opens its transaction.
// If `disp` already holds the bus, completes its previous band instead
// (normally LVGL already did, through flush_wait_hold_cb). Returns false if
// nothing may be sent; the caller then calls lv_display_flush_ready().
template <typename Select>
static bool holdPanelBus(lv_display_t* disp, lgfx::LGFX_Device* gfx, Select&& select) {
    if (s_panelHold.disp == disp) {
        completePanelDma();
        return true;
    }
    releasePanelBus();
    if (!select()) {
        // CS state unknown: send nothing, so pixels cannot land on another
        // panel. The self-heal timer sees the failure count change and repaints.
        g_display_mgr->deselectAllDisplays();
        return false;
    }
    gfx->startWrite();
    s_panelHold.disp = disp;
    s_panelHold.gfx = gfx;
    return true;
}

// Sends one band by DMA on the held bus; returns while it is going out.
// The draw buffers are DMA-capable (MALLOC_CAP_DMA).
static void startPanelDma(const lv_area_t* area, uint8_t* px_map) {
    const uint32_t width = area->x2 - area->x1 + 1;
    const uint32_t height = area->y2 - area->y1 + 1;
    s_panelHold.gfx->setAddrWindow(area->x1, area->y1, width, height);
    s_panelHold.gfx->writePixelsDMA(reinterpret_cast<uint16_t*>(px_map), width * height);
    s_panelHold.dmaPending = true;
    s_statFlushes++;
    s_panelHold.area = *area;
    s_panelHold.px = px_map;
}

// LVGL calls this (only while disp->flushing) before it reuses the buffer,
// and clears disp->flushing itself afterwards. Keeps the bus held: the next
// band normally goes to the same display.
static void flush_wait_hold_cb(lv_display_t* disp) {
    if (s_panelHold.disp == disp) {
        completePanelDma();
    }
}

// All minis render into one shared buffer: before any mini renders, a band
// still going out from that buffer must be done.
static void small_render_start_cb(lv_event_t*) {
    completePanelDma();
}

LvglDisplay::LvglDisplay()
    : main_display_(nullptr)
      , buffer1_(nullptr)
      , buffer2_(nullptr)
      , shared_small_buffer_(nullptr)
      , lvgl_mutex_(nullptr)
      , small_buffer_mutex_(nullptr)
      , initialized_(false)
      , main_screen_(nullptr)
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
    constexpr size_t buffer_size = MAIN_BUF_BYTES;
    buffer1_ = static_cast<uint8_t*>(heap_caps_aligned_alloc(32, buffer_size, MALLOC_CAP_DMA));
    if (MAIN_BUF_COUNT > 1) {
        buffer2_ = static_cast<uint8_t*>(heap_caps_aligned_alloc(32, buffer_size, MALLOC_CAP_DMA));
    }

    if (!buffer1_ || (MAIN_BUF_COUNT > 1 && !buffer2_)) {
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
    lv_display_set_flush_wait_cb(main_display_, flush_wait_hold_cb);

    lv_display_set_buffers(main_display_, buffer1_, buffer2_,
                          buffer_size, LV_DISPLAY_RENDER_MODE_PARTIAL);

    // Default theme, with the compiled font (LV_FONT_DEFAULT): firmware
    // text without a style of its own (modals) is a bitmap font, drawn at
    // once. The UI elements use config.json "styles" (ui_style.c).
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

    // See PanelBusHold. lv_display_flush_ready() follows when the DMA is done.
    if (!holdPanelBus(disp, main_gfx, [] { return g_display_mgr->selectMainDisplay(); })) {
        lv_display_flush_ready(disp);
        return;
    }
    startPanelDma(area, px_map);
}



void LvglDisplay::task() {
    if (!initialized_) return;

    lock();

    // Text that MQTT / JS tasks stored since the last tick (display item
    // values, button labels). Rendered here, under the lock this task already
    // holds, so those tasks never wait for the LVGL lock; lv_timer_handler()
    // then draws it in this same tick.
    freebutton::ui::applyPendingRenders();

    lv_timer_handler();

    if (s_screenshotRequested.exchange(false)) {
        captureScreens();
    }
    if (const int n = s_benchRequested.exchange(0); n > 0) {
        runBench(n);
    }

    // Process any pending WiFi QR modal operations
    wifi_qr_modal_process_pending();

    // Process any pending OTA modal operations
    ota_modal_process_pending();

    // lv_mem_monitor() walks the LVGL pool, so only this task (lock held)
    // may call it; other tasks read the snapshot via getLvglMemory().
    const uint32_t now = lv_tick_get();
    if (now - mem_sample_tick_ >= 1000u || lvgl_mem_total_.load(std::memory_order_relaxed) == 0) {
        mem_sample_tick_ = now;
        lv_mem_monitor_t mon;
        lv_mem_monitor(&mon);
        lvgl_mem_free_.store(mon.free_size, std::memory_order_relaxed);
        lvgl_mem_total_.store(mon.total_size, std::memory_order_relaxed);
    }

    unlock();
}

void LvglDisplay::requestScreenshot() {
    s_screenshotRequested.store(true);
}

// LVGL task, lock held. One full repaint per display, captured by the flush
// callbacks. A mini display without an LVGL buffer (no Button) is skipped.
void LvglDisplay::captureScreens() {
    auto& fs = getBusAwareFileSystem();
    const int64_t t0 = esp_timer_get_time();
    const size_t count = 1 + small_displays_.size();
    for (size_t i = 0; i < count; ++i) {
        lv_display_t* disp = i == 0 ? main_display_ : small_displays_[i - 1];
        if (!disp || !lv_display_get_buf_active(disp)) continue;
        lv_obj_t* scr = lv_display_get_screen_active(disp);
        if (!scr) continue;
        char path[40];
        if (i == 0) {
            snprintf(path, sizeof path, "/screenshot-main.rgb565");
        } else {
            snprintf(path, sizeof path, "/screenshot-mini%u.rgb565", static_cast<unsigned>(i - 1));
        }
        s_capFile = fs.open(path, FILE_WRITE, true);
        if (!s_capFile) {
            ESP_LOGE(TAG, "screenshot: cannot open %s", path);
            continue;
        }
        const uint16_t w = static_cast<uint16_t>(lv_display_get_horizontal_resolution(disp));
        const uint16_t h = static_cast<uint16_t>(lv_display_get_vertical_resolution(disp));
        const uint8_t hdr[kCapHeader] = {'F', 'B', 'S', 'S', static_cast<uint8_t>(w), static_cast<uint8_t>(w >> 8),
                                         static_cast<uint8_t>(h), static_cast<uint8_t>(h >> 8)};
        s_capOk = s_capFile.write(hdr, sizeof hdr) == sizeof hdr;
        s_capDisp = disp;
        lv_obj_invalidate(scr);
        lv_refr_now(disp);
        releasePanelBus();  // the last band is captured on completion
        s_capDisp = nullptr;
        s_capFile.close();
        s_capFile = BusAwareFile();
        ESP_LOGI(TAG, "screenshot: %s %ux%u %s", path, w, h, s_capOk ? "written" : "FAILED");
    }
    ESP_LOGI(TAG, "screenshot: done in %lld ms", (esp_timer_get_time() - t0) / 1000);
}

void LvglDisplay::requestBench(int repaints) {
    s_benchRequested.store(repaints < 1 ? 1 : (repaints > 50 ? 50 : repaints));
}

char* LvglDisplay::takeBenchResult(uint32_t* seq) {
    taskENTER_CRITICAL(&s_benchMux);
    char* json = s_benchJson;
    s_benchJson = nullptr;
    if (seq) *seq = s_benchSeq;
    taskEXIT_CRITICAL(&s_benchMux);
    return json;
}

// LVGL task, lock held. Each display (minis without an LVGL buffer are
// skipped) is repainted `n` times, like a screenshot without the file
// writes, every repaint timed to its last band on the wire. Per display:
// average and best time, the time waiting for DMA, bands, and glyphs
// rasterized with their cycles (fonts_stats).
void LvglDisplay::runBench(int n) {
    // On the heap, not the stack: the LVGL task's 8 KB stack is needed by
    // the repaints themselves (a 3 KB local overflowed it). Allocated per
    // run and freed by the reader (takeBenchResult).
    char* json = static_cast<char*>(malloc(kBenchJsonMax));
    if (!json) {
        ESP_LOGW(TAG, "bench: no memory for the result");
        return;
    }
    const size_t cap = kBenchJsonMax;
    size_t len = 0;
    const uint32_t seq = s_benchSeq + 1;
    len += snprintf(json + len, cap - len, "{\"seq\":%lu,\"repaints\":%d,\"displays\":[",
                    static_cast<unsigned long>(seq), n);
    const size_t count = 1 + small_displays_.size();
    bool first = true;
    for (size_t i = 0; i < count; ++i) {
        lv_display_t* disp = i == 0 ? main_display_ : small_displays_[i - 1];
        if (!disp || !lv_display_get_buf_active(disp)) continue;
        lv_obj_t* scr = lv_display_get_screen_active(disp);
        if (!scr) continue;
        releasePanelBus();
        int64_t total = 0, best = INT64_MAX;
        s_statFlushes = 0;
        s_statDmaWaitUs = 0;
        fonts_stats(nullptr, true);
        render_stats_get(nullptr, true);
        for (int r = 0; r < n; ++r) {
            const int64_t t0 = esp_timer_get_time();
            lv_obj_invalidate(scr);
            lv_refr_now(disp);
            releasePanelBus();  // the last band is on the wire too
            const int64_t dt = esp_timer_get_time() - t0;
            total += dt;
            if (dt < best) best = dt;
            vTaskDelay(1);  // the idle task runs (task watchdog), outside the timing
        }
        fonts_stats_t fs{};
        fonts_stats(&fs, true);
        render_stats_t rs{};
        render_stats_get(&rs, true);
        const uint32_t mhz = static_cast<uint32_t>(esp_clk_cpu_freq() / 1000000);
        char name[8];
        if (i == 0) {
            snprintf(name, sizeof name, "main");
        } else {
            snprintf(name, sizeof name, "mini%u", static_cast<unsigned>(i - 1));
        }
        len += snprintf(json + len, len < cap ? cap - len : 0,
                        "%s{\"display\":\"%s\",\"avg_us\":%lld,\"best_us\":%lld,\"dma_wait_us\":%lld,"
                        "\"bands\":%lu,\"glyphs\":%lu,\"glyph_us\":%llu,\"lookups\":%lu,\"lookup_us\":%llu,\"blend\":[",
                        first ? "" : ",", name, total / n, best, s_statDmaWaitUs / n,
                        static_cast<unsigned long>(s_statFlushes / n), static_cast<unsigned long>(fs.glyphs / n),
                        static_cast<unsigned long long>(fs.glyph_cycles / n / (mhz ? mhz : 240)),
                        static_cast<unsigned long>(fs.lookups / n),
                        static_cast<unsigned long long>(fs.lookup_cycles / n / (mhz ? mhz : 240)));
        // per blend kind (render_stats.h order) and the mask: [calls, pixels, us]
        for (int k = 0; k <= RENDER_BLEND_KINDS; ++k) {
            const render_stat_t& st = k < RENDER_BLEND_KINDS ? rs.blend[k] : rs.mask;
            len += snprintf(json + len, len < cap ? cap - len : 0, "%s[%lu,%lu,%llu]",
                            k ? "," : "", static_cast<unsigned long>(st.calls / n),
                            static_cast<unsigned long>(st.pixels / n),
                            static_cast<unsigned long long>(st.cycles / n / (mhz ? mhz : 240)));
        }
        len += snprintf(json + len, len < cap ? cap - len : 0, "]}");
        first = false;
    }
    if (len < cap) len += snprintf(json + len, cap - len, "]}");
    ESP_LOGI(TAG, "bench: %s", json);
    taskENTER_CRITICAL(&s_benchMux);
    char* unread = s_benchJson;  // an earlier result nobody fetched
    s_benchJson = json;
    s_benchSeq = seq;
    taskEXIT_CRITICAL(&s_benchMux);
    free(unread);
}

void LvglDisplay::getLvglMemory(size_t& freeBytes, size_t& totalBytes) const {
    freeBytes = lvgl_mem_free_.load(std::memory_order_relaxed);
    totalBytes = lvgl_mem_total_.load(std::memory_order_relaxed);
}

void LvglDisplay::lock() const {
    if (lvgl_mutex_) {
        xSemaphoreTake(lvgl_mutex_, portMAX_DELAY);
    }
}

void LvglDisplay::unlock() const {
    // Never leave a panel holding SPI2 / its CS once the lock is free (see
    // PanelBusHold). Flushes only run under this lock, so the task that
    // unlocks is the task that took the bus.
    releasePanelBus();
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
    constexpr size_t small_buffer_size = SMALL_BUF_BYTES;
    shared_small_buffer_ = static_cast<uint8_t*>(
        heap_caps_aligned_alloc(32, small_buffer_size, MALLOC_CAP_DMA)
    );
    if (SMALL_BUF_COUNT > 1) {
        shared_small_buffer2_ = static_cast<uint8_t*>(heap_caps_aligned_alloc(32, small_buffer_size, MALLOC_CAP_DMA));
    }

    if (!shared_small_buffer_ || (SMALL_BUF_COUNT > 1 && !shared_small_buffer2_)) {
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
        lv_display_set_flush_wait_cb(small_disp, flush_wait_hold_cb);
        lv_display_add_event_cb(small_disp, small_render_start_cb, LV_EVENT_RENDER_START, nullptr);
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

    // See PanelBusHold. lv_display_flush_ready() follows when the DMA is done.
    if (!holdPanelBus(disp, small_gfx, [&] { return g_display_mgr->selectSmallDisplay(connector, position); })) {
        lv_display_flush_ready(disp);
        return;
    }
    startPanelDma(area, px_map);
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

    releasePanelBus();  // a flush may still hold SPI2 and its CS

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

    constexpr size_t small_buffer_size = SMALL_BUF_BYTES;
    lv_display_set_buffers(disp, shared_small_buffer_, shared_small_buffer2_,
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

    releasePanelBus();  // a flush may still hold SPI2 and its CS

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

} // namespace FreeButton