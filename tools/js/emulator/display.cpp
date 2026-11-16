//
// display.cpp - the emulator's LVGL display driver, in place of the firmware's
// src/drivers/LvglDisplay.cpp (and DisplayManager.cpp).
//
// Kept as on the device (LvglDisplay::initialize, initializeSmallDisplays):
//   - one LVGL display per panel: the main display 320x240, a key display
//     160x80 for each side of each bar connector, in DisplayManager's order
//   - LV_COLOR_FORMAT_RGB565_SWAPPED, partial render mode, the firmware's draw
//     buffer sizes (main: 60 rows; the key displays share one of 30 rows,
//     handed over by acquireSmallDisplayBuffer / releaseSmallDisplayBuffer)
//   - the default dark theme with the compiled font, the black main screen,
//     init_fonts()
//   - task(): the pending renders, then lv_timer_handler(), then the pool
//     sample once a second
//
// Different:
//   - the flush: each band is converted from the panel's RGB565 (big-endian)
//     to RGBA in the display's frame buffer, which the host reads
//     (fbe_display_pixels). A 5 or 6 bit channel is widened by repeating its
//     top bits, so a colour has exactly the panel's 65536 values.
//   - the tick: the emulator's clock, latched once per pass (LVGL's timers and
//     animations see time stand still within one pass, so each timer runs at
//     most once per pass and a refresh draws each display at most once)
//
//   - two of LVGL's timers sleep while they have nothing to do, where the
//     device lets them run: the font reload poll (seam_fonts.c) and the
//     refresh timer of a key display without a draw buffer (below)
//
// Not here (hardware only): the SPI bus hold and DMA, the LVGL and buffer
// mutexes, the panels' self-heal sweep, backlight and sleep, the boot clear
// of the panels, the benchmark, the Wi-Fi QR and OTA modals.
//
#include <cstdlib>
#include <cstring>
#include <vector>

#include <lvgl.h>

#include "drivers/DisplayManager.h"
#include "drivers/LvglDisplay.h"
#include "emu.h"
#include "fbh.h"
#include "ui/PendingRender.h"

extern "C" {
#include "ui/fonts/fonts.h"
#include "ui/ui_text.h"
unsigned long millis(void);
}

// src/drivers/LvglDisplay.cpp
#define MAIN_DISPLAY_WIDTH 320
#define MAIN_DISPLAY_HEIGHT 240
#define MAIN_BUF_BYTES (MAIN_DISPLAY_WIDTH * 20 * 3)
#define SMALL_DISPLAY_WIDTH 160
#define SMALL_DISPLAY_HEIGHT 80
#define SMALL_BUF_BYTES (SMALL_DISPLAY_WIDTH * 30 * 2)

namespace {

struct Panel {
    lv_display_t* display = nullptr;
    int width = 0;
    int height = 0;
    uint8_t* rgba = nullptr;  // width * height * 4, rows top to bottom
    bool dirty = false;       // flushed since the host was last told
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
};

// [0] = the main display, [1 + i] = key display i.
std::vector<Panel> g_panels;
lv_obj_t* g_mainScreen = nullptr;
uint8_t* g_mainBuffer = nullptr;
uint8_t* g_smallBuffer = nullptr;
bool g_initialized = false;
uint32_t g_tick = 0;
uint32_t g_memSampleTick = 0;
// When LVGL's next timer is due (the tick), and whether there is one.
uint32_t g_nextTimerTick = 0;
bool g_hasTimer = false;
size_t g_memFree = 0;
size_t g_memTotal = 0;
size_t g_memLow = SIZE_MAX;
uint32_t g_flushes = 0;
uint32_t g_flushedPixels = 0;

uint32_t tickNow() { return g_tick; }

// LVGL's own log lines (LV_LOG_LEVEL_WARN and up, src/lv_conf.h), which the
// device prints on its UART: the firmware's warnings about fonts, the pool
// and text come this way.
void lvglLog(const lv_log_level_t level, const char* text) {
    size_t len = std::strlen(text);
    while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r')) --len;
    fbh_log(level >= LV_LOG_LEVEL_ERROR ? 'E' : level == LV_LOG_LEVEL_WARN ? 'W' : 'I', "lvgl", text, len);
}

void sampleMemory() {
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    g_memFree = mon.free_size;
    g_memTotal = mon.total_size;
    if (mon.free_size < g_memLow) g_memLow = mon.free_size;
}

// The panel's pixel (RGB565, high byte first) as RGBA.
void store(Panel& panel, const lv_area_t* area, const uint8_t* px) {
    const int w = area->x2 - area->x1 + 1;
    for (int y = area->y1; y <= area->y2; ++y) {
        if (y < 0 || y >= panel.height) {
            px += static_cast<size_t>(w) * 2;
            continue;
        }
        uint8_t* out = panel.rgba + (static_cast<size_t>(y) * panel.width + area->x1) * 4;
        for (int x = area->x1; x <= area->x2; ++x, px += 2, out += 4) {
            if (x < 0 || x >= panel.width) continue;
            const unsigned v = (static_cast<unsigned>(px[0]) << 8) | px[1];
            const unsigned r = v >> 11, g = (v >> 5) & 63u, b = v & 31u;
            out[0] = static_cast<uint8_t>((r << 3) | (r >> 2));
            out[1] = static_cast<uint8_t>((g << 2) | (g >> 4));
            out[2] = static_cast<uint8_t>((b << 3) | (b >> 2));
            out[3] = 255;
        }
    }
    if (!panel.dirty) {
        panel.dirty = true;
        panel.x1 = area->x1; panel.y1 = area->y1; panel.x2 = area->x2; panel.y2 = area->y2;
    } else {
        if (area->x1 < panel.x1) panel.x1 = area->x1;
        if (area->y1 < panel.y1) panel.y1 = area->y1;
        if (area->x2 > panel.x2) panel.x2 = area->x2;
        if (area->y2 > panel.y2) panel.y2 = area->y2;
    }
    ++g_flushes;
    g_flushedPixels += static_cast<uint32_t>(w) * static_cast<uint32_t>(area->y2 - area->y1 + 1);
}

void flushMain(lv_display_t* disp, const lv_area_t* area, uint8_t* px) {
    store(g_panels[0], area, px);
    lv_display_flush_ready(disp);
}

void flushSmall(lv_display_t* disp, const lv_area_t* area, uint8_t* px) {
    // As flush_small_cb: the display's index is its user data.
    const size_t index = reinterpret_cast<uintptr_t>(lv_display_get_user_data(disp));
    if (index + 1 < g_panels.size()) store(g_panels[index + 1], area, px);
    lv_display_flush_ready(disp);
}

Panel makePanel(lv_display_t* display, const int width, const int height) {
    Panel panel;
    panel.display = display;
    panel.width = width;
    panel.height = height;
    const size_t bytes = static_cast<size_t>(width) * height * 4;
    panel.rgba = static_cast<uint8_t*>(std::malloc(bytes));
    // A panel after its boot clear: black.
    for (size_t i = 0; i < bytes; i += 4) {
        panel.rgba[i] = panel.rgba[i + 1] = panel.rgba[i + 2] = 0;
        panel.rgba[i + 3] = 255;
    }
    return panel;
}

// A key display without a key in config.json never gets a draw buffer
// (Button.cpp asks for it). LVGL's refresh timer of such a display then never
// pauses: on the device it runs every 33 ms and logs "No draw buffer" each
// time. Here it sleeps until the display has a buffer (an invalidation wakes
// it), so an idle emulator has no timer and no log flood.
void sleepUnbufferedDisplays() {
    for (size_t i = 1; i < g_panels.size(); ++i) {
        if (lv_display_get_buf_active(g_panels[i].display) != nullptr) continue;
        if (lv_timer_t* timer = lv_display_get_refr_timer(g_panels[i].display)) lv_timer_pause(timer);
    }
}

// Tells the host which part of each display changed since the last call.
void reportFrames() {
    for (size_t i = 0; i < g_panels.size(); ++i) {
        Panel& p = g_panels[i];
        if (!p.dirty) continue;
        p.dirty = false;
        fbh_frame(static_cast<int>(i), p.x1, p.y1, p.x2 - p.x1 + 1, p.y2 - p.y1 + 1);
    }
}

} // namespace

namespace FreeButton {

LvglDisplay& getLvglDisplay() {
    static LvglDisplay display;
    return display;
}

DisplayManager& getDisplayManager() {
    static DisplayManager manager;
    return manager;
}

bool initializeLvgl(DisplayManager& displayManager) { return getLvglDisplay().initialize(displayManager); }

bool LvglDisplay::initialize(DisplayManager& displayManager) {
    if (g_initialized) return true;
    g_tick = static_cast<uint32_t>(millis());
    lv_init();
    lv_tick_set_cb(tickNow);
    lv_log_register_print_cb(lvglLog);

    g_mainBuffer = static_cast<uint8_t*>(std::malloc(MAIN_BUF_BYTES));
    lv_display_t* main = lv_display_create(MAIN_DISPLAY_WIDTH, MAIN_DISPLAY_HEIGHT);
    if (!g_mainBuffer || !main) return false;
    lv_display_set_color_format(main, LV_COLOR_FORMAT_RGB565_SWAPPED);
    lv_display_set_flush_cb(main, flushMain);
    lv_display_set_buffers(main, g_mainBuffer, nullptr, MAIN_BUF_BYTES, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_theme_t* theme = lv_theme_default_init(main, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED),
                                              LV_THEME_DEFAULT_DARK, LV_FONT_DEFAULT);
    lv_display_set_theme(main, theme);
    g_panels.push_back(makePanel(main, MAIN_DISPLAY_WIDTH, MAIN_DISPLAY_HEIGHT));

    // initializeSmallDisplays()
    g_smallBuffer = static_cast<uint8_t*>(std::malloc(SMALL_BUF_BYTES));
    const size_t count = displayManager.getSmallDisplayCount();
    for (size_t i = 0; i < count; ++i) {
        lv_display_t* small = lv_display_create(SMALL_DISPLAY_WIDTH, SMALL_DISPLAY_HEIGHT);
        if (!small) break;
        lv_display_set_color_format(small, LV_COLOR_FORMAT_RGB565_SWAPPED);
        lv_display_set_flush_cb(small, flushSmall);
        lv_display_set_user_data(small, reinterpret_cast<void*>(i));
        g_panels.push_back(makePanel(small, SMALL_DISPLAY_WIDTH, SMALL_DISPLAY_HEIGHT));
    }

    g_mainScreen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(g_mainScreen, lv_color_hex(0x000000), 0);
    lv_scr_load(g_mainScreen);

    init_fonts();
    sampleMemory();
    g_memSampleTick = g_tick;
    g_initialized = true;
    return true;
}

void LvglDisplay::task() {
    if (!g_initialized) return;
    g_tick = static_cast<uint32_t>(millis());
    freebutton::ui::applyPendingRenders();
    fbe_fonts_poll_sleep();
    sleepUnbufferedDisplays();
    const uint32_t wait = lv_timer_handler();
    g_hasTimer = wait != LV_NO_TIMER_READY;
    g_nextTimerTick = g_tick + wait;
    if (g_tick - g_memSampleTick >= 1000u) {
        g_memSampleTick = g_tick;
        sampleMemory();
    } else {
        // The lowest point is the one that matters to "does it still fit".
        lv_mem_monitor_t mon;
        lv_mem_monitor(&mon);
        if (mon.free_size < g_memLow) g_memLow = mon.free_size;
    }
    reportFrames();
}

lv_display_t* LvglDisplay::getMainDisplay() const { return g_panels.empty() ? nullptr : g_panels[0].display; }
lv_obj_t* LvglDisplay::getMainScreen() const { return g_mainScreen; }
bool LvglDisplay::isInitialized() const { return g_initialized; }
size_t LvglDisplay::getSmallDisplayCount() const { return g_panels.empty() ? 0 : g_panels.size() - 1; }

lv_display_t* LvglDisplay::getSmallDisplay(const int index) const {
    return index >= 0 && static_cast<size_t>(index) + 1 < g_panels.size() ? g_panels[index + 1].display : nullptr;
}

bool LvglDisplay::acquireSmallDisplayBuffer(const int index) const {
    lv_display_t* disp = getSmallDisplay(index);
    if (!disp || !g_smallBuffer) return false;
    lv_display_set_buffers(disp, g_smallBuffer, nullptr, SMALL_BUF_BYTES, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_enable_invalidation(disp, true);
    return true;
}

void LvglDisplay::releaseSmallDisplayBuffer(const int index) const {
    if (lv_display_t* disp = getSmallDisplay(index)) lv_display_enable_invalidation(disp, false);
}

void LvglDisplay::getLvglMemory(size_t& freeBytes, size_t& totalBytes) const {
    freeBytes = g_memFree;
    totalBytes = g_memTotal;
}

int DisplayManager::getSmallDisplayIndexByPosition(const int buttonPosition) const {
    if (buttonPosition < 1) return -1;
    const int connector = (buttonPosition - 1) / 2;
    const int side = (buttonPosition - 1) % 2;
    for (size_t i = 0; i < smallDisplays_.size(); ++i) {
        if (smallDisplays_[i].connector == connector && smallDisplays_[i].position == side) return static_cast<int>(i);
    }
    return -1;
}

// The firmware's initializeDisplays(): two key displays per bar connector.
bool initializeDisplays(const std::vector<std::shared_ptr<Adafruit_MCP23X08>>& mcps, const int tft320Bar) {
    auto& manager = getDisplayManager();
    manager.clear();
    manager.setMainConnector(tft320Bar);
    for (size_t connector = 0; connector < mcps.size(); ++connector) {
        if (!mcps[connector] || static_cast<int>(connector) == tft320Bar) continue;
        for (int side = 0; side < 2; ++side) manager.add({static_cast<int>(connector), side});
    }
    return true;
}

} // namespace FreeButton

// --- the host's view ------------------------------------------------------------

// Milliseconds until LVGL has a timer to run (a refresh after a change, an
// animation step, the clock's hands, the font reload poll): when the host
// calls the LVGL task next. 0xFFFFFFFF: nothing is scheduled.
FBE_EXPORT(fbe_display_wait) uint32_t fbe_display_wait(void) {
    if (!g_initialized || !g_hasTimer) return 0xFFFFFFFFu;
    const uint32_t now = static_cast<uint32_t>(millis());
    const int32_t left = static_cast<int32_t>(g_nextTimerTick - now);
    // The firmware's LVGL task sleeps FB_LVGL_TASK_MS between its turns.
    return left > FB_LVGL_TASK_MS ? static_cast<uint32_t>(left) : FB_LVGL_TASK_MS;
}

// Draws what is pending now, without waiting for the refresh period: the
// firmware's screenshot does the same (lv_refr_now) before it reads a panel.
// A key display without a key never got a draw buffer and stays black.
void fbe_display_refresh_now(void) {
    if (!g_initialized) return;
    g_tick = static_cast<uint32_t>(millis());
    freebutton::ui::applyPendingRenders();
    fbe_fonts_poll_sleep();
    for (Panel& panel : g_panels) {
        if (lv_display_get_buf_active(panel.display) != nullptr) lv_refr_now(panel.display);
    }
    reportFrames();
}

// Displays: 0 = the main display, 1 + i = key display i.
FBE_EXPORT(fbe_display_count) int fbe_display_count(void) { return static_cast<int>(g_panels.size()); }

// out: width, height, connector (-1: the main display), side (0 left, 1 right).
FBE_EXPORT(fbe_display_info) int fbe_display_info(const int display, int32_t* out) {
    if (display < 0 || static_cast<size_t>(display) >= g_panels.size()) return 0;
    out[0] = g_panels[display].width;
    out[1] = g_panels[display].height;
    out[2] = -1;
    out[3] = 0;
    if (display > 0) {
        const auto& smalls = FreeButton::getDisplayManager().smallDisplays();
        if (static_cast<size_t>(display - 1) < smalls.size()) {
            out[2] = smalls[display - 1].connector;
            out[3] = smalls[display - 1].position;
        }
    }
    return 1;
}

// The display's frame buffer: RGBA, width * height * 4 bytes, valid until the
// module is discarded (it never moves).
FBE_EXPORT(fbe_display_pixels) const uint8_t* fbe_display_pixels(const int display) {
    return display >= 0 && static_cast<size_t>(display) < g_panels.size() ? g_panels[display].rgba : nullptr;
}

// A full repaint of one display at its next refresh (-1: all), as the
// firmware's screenshot and self-heal do.
FBE_EXPORT(fbe_display_invalidate) void fbe_display_invalidate(const int display) {
    for (size_t i = 0; i < g_panels.size(); ++i) {
        if (display >= 0 && static_cast<size_t>(display) != i) continue;
        if (lv_obj_t* screen = lv_display_get_screen_active(g_panels[i].display)) lv_obj_invalidate(screen);
    }
}

// out[10]: pool free now, pool total, lowest free seen, flushes, flushed
// pixels, texts drawn in one format / not drawn for lack of pool
// (ui_text_stats), bytes of media held, font instances, animations running.
FBE_EXPORT(fbe_lvgl_stats) void fbe_lvgl_stats(uint32_t* out) {
    if (g_initialized) sampleMemory();
    out[0] = static_cast<uint32_t>(g_memFree);
    out[1] = static_cast<uint32_t>(g_memTotal);
    out[2] = static_cast<uint32_t>(g_memLow == SIZE_MAX ? 0 : g_memLow);
    out[3] = g_flushes;
    out[4] = g_flushedPixels;
    uint32_t collapsed = 0, skipped = 0;
    ui_text_stats(&collapsed, &skipped);
    out[5] = collapsed;
    out[6] = skipped;
    out[7] = fbe_media_bytes();
    out[8] = static_cast<uint32_t>(fbe_font_instance_count());
    out[9] = lv_anim_count_running();
}

// For tests of the firmware's low-memory paths: takes `bytes` out of the LVGL
// pool (0 gives them back). Returns what is free afterwards.
FBE_EXPORT(fbe_lvgl_hold) uint32_t fbe_lvgl_hold(const uint32_t bytes) {
    static std::vector<void*> held;
    for (void* p : held) lv_free(p);
    held.clear();
    uint32_t left = bytes;
    while (left > 0) {
        const uint32_t chunk = left > 1024u ? 1024u : left;
        void* p = lv_malloc(chunk);
        if (!p) break;
        held.push_back(p);
        left -= chunk;
    }
    sampleMemory();
    return static_cast<uint32_t>(g_memFree);
}
