//
// Created by Bolke de Bruin on 27/11/2025.
//

#include "Clock.h"

#include "drivers/LvglDisplay.h"
#include "mqtt/Registry.h"

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

void Clock::createClock() const {
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

    lvglDisplay.unlock();

    ESP_LOGI(TAG, "Clock %lu created at (%d,%d) with size %dx%d on page %d",
             id, x, y, width, height, page);
}

void Clock::destroyClock() {
    clock_destroy();
}


} // namespace freebutton