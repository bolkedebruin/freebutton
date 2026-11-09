//
// Created by Bolke de Bruin on 18/11/2025.
//

#ifndef FREEBUTTON_DISPLAYITEM_H
#define FREEBUTTON_DISPLAYITEM_H

#include "mqtt/Component.h"
#include "TopicHandler.h"
#include "ui/PendingRender.h"
#include "utils/TextSlot.h"
#include <cstdlib>
#include <string>
#include <vector>

#include "ArduinoJson/Object/JsonObject.hpp"
#include "json/JsonWriter.h"

extern "C" {
#include "ui/ui.h"
}

namespace freebutton {

class DisplayItem final : public MqttSubscriber, public TopicHandler<DisplayItem>, public ui::PendingRender {
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
    // The UI box; exists only while the item's page is shown (built and
    // deleted in applyPending()). LVGL lock held.
    lv_obj_t* box = nullptr;
    // A build was skipped for lack of LVGL memory; logged once (LVGL lock held).
    bool buildFailed_ = false;

    // Text set from other tasks (MQTT, JS), rendered by the LVGL task in
    // applyPending(). Sizes are whole footprints (see TextSlot.h):
    //  * value, 64 B (58 text bytes): one full 320 px line of the smallest
    //    font (~45 glyphs at ~7 px) plus a \cRRGGBB colour code and an MDI
    //    icon. Longer text would not fit on screen anyway and would cost
    //    ~76 B of LVGL pool per byte to render.
    //  * caption and unit, 32 B (26 text bytes) each. Plain members, so
    //    nothing is ever allocated (or freed) on the message path.
    // The configured label / unit strings above stay the defaults that
    // a newly built box draws; these slots are the runtime state.
    static constexpr size_t kValueSlotBytes = 64;
    static constexpr size_t kCaptionSlotBytes = 32;
    static constexpr size_t kUnitSlotBytes = 32;
    TextSlot<kValueSlotBytes> valueSlot_;
    TextSlot<kCaptionSlotBytes> captionSlot_;
    TextSlot<kUnitSlotBytes> unitSlot_;

    // Unlinks and deletes the box (destructor). Takes the LVGL lock.
    void detach();
    // LVGL lock held.
    bool buildBox();
    void deleteBox();
    void drawConfigured();  // box exists

public:

    DisplayItem(int x, int y, int width, int boxtype, int fontSize, int alignment,
                std::string  label, std::string  unit, int round, int page,
                std::vector<Topic> topicConfigs);

    ~DisplayItem();

    void onMessage(uint8_t brokerId, std::string_view topic, std::string_view data) override;
    const std::string getComponentType() const override;

    // Id given in the config ("displayitemid", a number or a numeric string
    // as the Button+ format writes it), or 0 when absent or invalid. Scripts
    // address config items by this id, so it must stay stable across boots.
    static unsigned long configuredId(const JsonObject& config) {
        const auto v = config["displayitemid"];
        if (v.is<unsigned long>()) {
            return v.as<unsigned long>();
        }
        if (v.is<const char*>()) {
            const char* s = v.as<const char*>();
            if (s == nullptr || *s < '0' || *s > '9') {
                return 0;
            }
            char* end = nullptr;
            const unsigned long id = std::strtoul(s, &end, 10);
            return (end != nullptr && *end == '\0') ? id : 0;
        }
        return 0;
    }

    // Auto ids (items without a configured id, JS display.create()) start
    // above every configured id, so they never collide with one.
    static void reserveIdsUpTo(const unsigned long maxConfiguredId) {
        if (maxConfiguredId >= next_id) {
            next_id = maxConfiguredId + 1;
        }
    }

    static std::unique_ptr<DisplayItem> fromJson(const JsonObject& config) {
        auto displayItem = std::make_unique<DisplayItem>();
        displayItem->configure(config);
        displayItem->attach();
        return displayItem;
    }

    template<typename OutputStream>
    void toJson(json::JsonWriter<OutputStream>::ScopedObject &obj) {
        obj.Add("displayitemid", std::to_string(id));
        obj.Add("x", x);
        obj.Add("y", y);
        obj.Add("page", page);
        obj.Add("boxtype", boxType);
        obj.Add("fontsize", fontSize);
        obj.Add("align", alignment);
        obj.Add("width", width);
        obj.Add("label", label);
        obj.Add("unit", unit);
        obj.Add("round", round);

        // Serialize topics array
        {
            auto arr = obj.Array("topics");
            for (auto &topic: topics) {
                auto item = arr.Object();
                topic.toJson<OutputStream>(item);
            }
        }
    }

private:
    static constexpr auto TAG = "DisplayItem";
    std::vector<Topic> topics;

    void configure(const JsonObject &data) {
        if (const unsigned long configured = configuredId(data); configured != 0) {
            id = configured;
        }
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
            // Normal for items whose value a script sets.
            ESP_LOGD(TAG, "DisplayItem %lu has no topics", id);
        }
    }
public:
    // CRTP interface for TopicHandler
    static const char* getComponentName() { return "DisplayItem"; }
    std::vector<Topic>& getTopicsRef() { return topics; }
    void registerTopics();
    void unregisterTopics();

    // Links the item for rendering; its box is built when its page is shown
    // (and deleted when it goes, so a hidden item costs no LVGL memory). Call
    // after configuration, and again after a change to position, size or
    // the configured label/unit: a shown box is redrawn. Takes the LVGL lock.
    void attach();

    // Programmatic updates (JS). Any task: they only store the text; the
    // LVGL task renders it on its next tick. No LVGL lock is taken.
    void setDisplayLabel(std::string_view text);
    void setDisplayValue(std::string_view text);
    void setDisplayUnit(std::string_view text);

    // Page (config "page"; 0 = every page). Any task: the LVGL task hides or
    // shows the box on its next tick (see ui/MainPage.h).
    void setPage(int newPage);

    // PendingRender (LVGL task, LVGL lock held)
    bool applyPending() override;

};

} // freebutton

#endif //FREEBUTTON_DISPLAYITEM_H