//
// Created by Bolke de Bruin on 27/11/2025.
//

#ifndef FREEBUTTON_CLOCK_H_COMPONENT
#define FREEBUTTON_CLOCK_H_COMPONENT

#include "mqtt/Component.h"
#include "ui/PendingRender.h"
#include <string>
#include <memory>

#include "ArduinoJson.h"
#include "json/JsonWriter.h"

extern "C" {
#include "ui/ui_clock.h"
}

namespace freebutton {

// A PendingRender only to follow the main display's page (ui/MainPage.h):
// it has no text set from other tasks.
class Clock final : public ui::PendingRender {
public:
    static unsigned long next_id;

    unsigned long id;
    int x, y;
    int width, height;
    int page = 1;
    lv_align_t align = LV_ALIGN_TOP_LEFT;

    Clock() : Clock(0, 0, 260, 260, 1) {}

    Clock(int x, int y, int width, int height, int page);
    ~Clock();

    static std::unique_ptr<Clock> fromJson(const ArduinoJson::JsonObject& config) {
        auto clock = std::make_unique<Clock>();
        clock->configure(config);
        clock->createClock();  // Create UI clock after configuration
        return clock;
    }

    template<typename OutputStream>
    void toJson(json::JsonWriter<OutputStream>::ScopedObject &obj) {
        obj.Add("clockid", std::to_string(id));
        obj.Add("x", x);
        obj.Add("y", y);
        obj.Add("width", width);
        obj.Add("height", height);
        obj.Add("page", page);
        obj.Add("align", static_cast<int>(align));
    }

private:
    static constexpr auto TAG = "Clock";
    bool hiddenByPage_ = false;  // LVGL lock held
    void applyPage();

    void configure(const JsonObject &data) {
        x = data["x"].isNull() ? x : data["x"].as<int>();
        y = data["y"].isNull() ? y : data["y"].as<int>();
        width = data["width"].isNull() ? width : data["width"].as<int>();
        height = data["height"].isNull() ? height : data["height"].as<int>();
        page = data["page"].isNull() ? page : data["page"].as<int>();
        align = data["align"].isNull() ? LV_ALIGN_TOP_LEFT : static_cast<lv_align_t>(data["align"].as<int>());
    }

public:
    // UI methods
    void createClock();
    void destroyClock();

    // PendingRender (LVGL task, LVGL lock held): hide / show for the shown page.
    bool applyPending() override;

    // Getters for ConfigManager
    int getPage() const { return page; }
    int getX() const { return x; }
    int getY() const { return y; }
    int getWidth() const { return width; }
    int getHeight() const { return height; }
};

} // namespace freebutton

#endif //FREEBUTTON_CLOCK_H_COMPONENT