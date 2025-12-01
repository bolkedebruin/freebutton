//
// Created by Bolke de Bruin on 27/11/2025.
//

#include "Clock.h"

#include "drivers/LvglDisplay.h"
#include "mqtt/Registry.h"

extern "C" {
#include "ui/ui.h"
#include "ui/ui_clock.h"

// Access to global timer and callback from ui_clock.c
extern lv_timer_t* g_timer;
extern void clock_timer_cb(lv_timer_t* timer);
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

void Clock::onMessage(const uint8_t brokerId, const std::string_view topic, const std::string_view data) {
    // Clock doesn't need to handle MQTT messages - it displays system time
    // This method is required by MqttSubscriber interface but remains empty
}

const std::string Clock::getComponentType() const {
    return "Clock";
}

void Clock::createClock() const {
    if (clock_widget) {
        ESP_LOGW(TAG, "Clock %lu already created", id);
        return;
    }

    auto& lvglDisplay = FreeButton::getLvglDisplay();
    lv_obj_t* parent = lvglDisplay.getMainScreen();

    if (!parent) {
        ESP_LOGE(TAG, "No active screen available for Clock %lu", id);
        return;
    }

    lvglDisplay.lock();

    // Create the clock using our ui_clock.h functions
    clock_init(parent, x, y);

    lvglDisplay.unlock();

    // Start FreeRTOS update task instead of LVGL timer
    //startUpdateTask();

    ESP_LOGI(TAG, "Clock %lu created at (%d,%d) with size %dx%d on page %d",
             id, x, y, width, height, page);
}

void Clock::destroyClock() {

    if (clock_widget) {
        const auto& lvglDisplay = FreeButton::getLvglDisplay();
        lvglDisplay.lock();

        // The clock cleanup will handle timer and UI cleanup
        if (clock_widget->scale) {
            lv_obj_del(clock_widget->scale);
        }
        clock_widget = nullptr;
        lvglDisplay.unlock();
        ESP_LOGI(TAG, "Clock %lu destroyed", id);
    }
}


} // namespace freebutton