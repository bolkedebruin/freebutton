//
// Created by Bolke de Bruin on 18/11/2025.
//

#ifndef FREEBUTTON_DISPLAYITEM_H
#define FREEBUTTON_DISPLAYITEM_H

#include "mqtt/Component.h"
#include "TopicHandler.h"
#include <string>
#include <vector>

#include "ArduinoJson/Object/JsonObject.hpp"

extern "C" {
#include "ui/ui.h"
}

namespace freebutton {

class DisplayItem final : public MqttSubscriber, public TopicHandler<DisplayItem> {
public:
    static unsigned long next_id;

    unsigned long id;

    int x, y;
    int width;
    int boxType;

    int fontSize;
    int alignment;

    std::string label;
    std::string unit;
    int round;

    int page = 1;

    DisplayItem() : DisplayItem(0, 0, 100, 0, 14, 0, "", "", 0, 1, {}) {}

private:
    lv_obj_t* box = nullptr;  // UI box element

public:

    DisplayItem(int x, int y, int width, int boxtype, int fontSize, int alignment,
                std::string  label, std::string  unit, int round, int page,
                std::vector<Topic> topicConfigs);

    ~DisplayItem();

    void onMessage(uint8_t brokerId, std::string_view topic, std::string_view data) override;
    const std::string getComponentType() const override;

    static std::unique_ptr<DisplayItem> fromJson(const JsonObject& config) {
        auto displayItem = std::make_unique<DisplayItem>();
        displayItem->configure(config);
        displayItem->createBox();  // Create UI box after configuration
        return displayItem;
    }

private:
    static constexpr auto TAG = "DisplayItem";
    std::vector<Topic> topics;

    void configure(const JsonObject &data) {
        x = data["x"].isNull() ? x : data["x"].as<int>();
        y = data["y"].isNull() ? y : data["y"].as<int>();

        boxType = data["boxtype"].isNull() ? boxType : data["boxtype"].as<int>();
        fontSize = data["fontsize"].isNull() ? fontSize : data["fontsize"].as<int>();

        alignment = data["align"].isNull() ? alignment : data["align"].as<int>();
        width = data["width"].isNull() ? width : data["width"].as<int>();

        round = data["round"].isNull() ? round : data["round"].as<int>();

        label = data["label"].isNull() ? label : data["label"].as<std::string>();
        unit = data["unit"].isNull() ? unit : data["unit"].as<std::string>();
        page = data["page"].isNull() ? page : data["page"].as<int>();

        if (data["topics"].is<JsonArray>()) {
            handleTopics(data["topics"].as<JsonArray>());
        } else {
            ESP_LOGW(TAG, "No topics in DisplayItem JSON");
        }
    }
public:
    // CRTP interface for TopicHandler
    static const char* getComponentName() { return "DisplayItem"; }
    std::vector<Topic>& getTopicsRef() { return topics; }
    void registerTopics();
    void unregisterTopics();

    // UI methods
    void createBox();
    void destroyBox();

};

} // freebutton

#endif //FREEBUTTON_DISPLAYITEM_H