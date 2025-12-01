//
// Created by Bolke de Bruin on 27/11/2025.
//

#ifndef FREEBUTTON_CLOCK_H_COMPONENT
#define FREEBUTTON_CLOCK_H_COMPONENT

#include "mqtt/Component.h"
#include <string>
#include <memory>

#include "ArduinoJson.h"

extern "C" {
#include "ui/ui_clock.h"
}

namespace freebutton {

class Clock final : public MqttSubscriber {
public:
    static unsigned long next_id;

    unsigned long id;
    int x, y;
    int width, height;
    int page = 1;

private:
    analog_clock_t* clock_widget = nullptr;  // Clock UI element
    TaskHandle_t updateTask = nullptr;       // FreeRTOS task for updates
    volatile bool shouldStop = false;       // Task control flag

public:
    Clock() : Clock(0, 0, 260, 260, 1) {}

    Clock(int x, int y, int width, int height, int page);
    ~Clock();

    void onMessage(uint8_t brokerId, std::string_view topic, std::string_view data) override;
    const std::string getComponentType() const override;

    static std::unique_ptr<Clock> fromJson(const ArduinoJson::JsonObject& config) {
        auto clock = std::make_unique<Clock>();
        clock->configure(config);
        clock->createClock();  // Create UI clock after configuration
        return clock;
    }

private:
    static constexpr auto TAG = "Clock";

    void configure(const ArduinoJson::JsonObject &data) {
        x = data["x"].isNull() ? x : data["x"].as<int>();
        y = data["y"].isNull() ? y : data["y"].as<int>();
        width = data["width"].isNull() ? width : data["width"].as<int>();
        height = data["height"].isNull() ? height : data["height"].as<int>();
        page = data["page"].isNull() ? page : data["page"].as<int>();
    }

private:
    // Task management
    void startUpdateTask();
    void stopUpdateTask();
    static void updateTaskFunction(void* parameter);

public:
    // UI methods
    void createClock();
    void destroyClock();

    // Getters for ComponentManager
    int getPage() const { return page; }
    int getX() const { return x; }
    int getY() const { return y; }
    int getWidth() const { return width; }
    int getHeight() const { return height; }
};

} // namespace freebutton

#endif //FREEBUTTON_CLOCK_H_COMPONENT