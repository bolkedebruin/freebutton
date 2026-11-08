//
// Created by Bolke de Bruin on 27/11/2025.
//

#include "Clock.h"

#include "drivers/LvglDisplay.h"
#include "mqtt/Registry.h"
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
    destroyClock();
}

void Clock::createClock() {
    /*
    if (clock_widget) {
        ESP_LOGW(TAG, "Clock %lu already created", id);
        return;
    }
    */

    auto& lvglDisplay = FreeButton::getLvglDisplay();
    lv_obj_t* parent = lvglDisplay.getMainScreen();

    if (!parent) {
        ESP_LOGE(TAG, "No active screen available for Clock %lu", id);
        return;
    }

    lvglDisplay.lock();

    clock_init(parent, x, y, width, height, align);
    hiddenByPage_ = false;
    applyPage();
    linkPending();  // follows later page changes

    lvglDisplay.unlock();

    ESP_LOGI(TAG, "Clock %lu created at (%d,%d) with size %dx%d on page %d",
             id, x, y, width, height, page);
}

void Clock::destroyClock() {
    // Deletes LVGL objects and a timer: must hold the (non-recursive) LVGL
    // lock, so ~Clock must not run while the caller holds it.
    auto& lvglDisplay = FreeButton::getLvglDisplay();
    lvglDisplay.lock();
    unlinkPending();
    clock_destroy();
    lvglDisplay.unlock();
}

// LVGL lock held.
void Clock::applyPage() {
    const bool hide = !ui::visibleOnPage(page, ui::shownPage());
    if (hide != hiddenByPage_) {
        clock_set_hidden(hide);
        hiddenByPage_ = hide;
    }
}

bool Clock::applyPending() {
    applyPage();
    return false;
}


} // namespace freebutton