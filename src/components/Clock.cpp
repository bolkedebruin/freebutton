//
// Created by Bolke de Bruin on 27/11/2025.
//

#include "Clock.h"

#include "drivers/LvglDisplay.h"
#include "mqtt/Registry.h"
#include "ui/LvglRoom.h"
#include "ui/MainPage.h"

extern "C" {
#include "ui/ui_clock.h"
}

namespace freebutton {

unsigned long Clock::next_id = 1;

Clock::Clock(const int x, const int y, const int width, const int height, const int page)
    : id(next_id++)
    , x(x), y(y), width(width), height(height), page(page)
{
    // Clock will be created after configuration
}

Clock::~Clock() {
    detach();
}

namespace {
// The clock that is drawn: ui_clock draws a single clock, so a second one in
// the config is left out (as before). LVGL lock held.
Clock* g_drawnClock = nullptr;
} // namespace

void Clock::attach() {
    auto& lvglDisplay = FreeButton::getLvglDisplay();
    lvglDisplay.lock();
    linkPending();  // follows page changes
    if (g_drawnClock == nullptr) {
        g_drawnClock = this;
    } else if (g_drawnClock != this) {
        ESP_LOGW(TAG, "Clock %lu: only one clock is drawn, this one is left out", id);
    }
    lvglDisplay.unlock();
    ui::requestPendingRender();  // builds the clock if its page is shown
}

void Clock::detach() {
    // Deletes LVGL objects and a timer: must hold the (non-recursive) LVGL
    // lock, so ~Clock must not run while the caller holds it.
    auto& lvglDisplay = FreeButton::getLvglDisplay();
    lvglDisplay.lock();
    unlinkPending();
    if (g_drawnClock == this) {
        deleteClock();
        g_drawnClock = nullptr;
    }
    lvglDisplay.unlock();
}

// LVGL task, LVGL lock held. The clock (objects and timer) exists only while
// its page is shown.
bool Clock::applyPending() {
    if (g_drawnClock != this) {
        return false;
    }
    if (!ui::visibleOnPage(page, ui::shownPage())) {
        deleteClock();
    } else if (!built_) {
        buildClock();
    }
    return false;
}

// LVGL lock held.
void Clock::buildClock() {
    lv_obj_t* parent = FreeButton::getLvglDisplay().getMainScreen();
    if (parent == nullptr) {
        return;
    }
    if (!ui::lvglHasRoom(ui::kRoomForClock)) {
        if (!buildFailed_) {
            ESP_LOGE(TAG, "Clock %lu: no LVGL memory to show it", id);
            buildFailed_ = true;
        }
        return;  // the next render request retries
    }
    buildFailed_ = false;
    built_ = clock_init(parent, x, y, width, height, align);
}

// LVGL lock held.
void Clock::deleteClock() {
    if (built_) {
        clock_destroy();
        built_ = false;
    }
}

} // namespace freebutton