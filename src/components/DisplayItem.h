//
// Created by Bolke de Bruin on 18/11/2025.
//

#ifndef FREEBUTTON_DISPLAYITEM_H
#define FREEBUTTON_DISPLAYITEM_H

#include "mqtt/Component.h"
#include "TopicHandler.h"
#include "IconRef.h"
#include "StyleRef.h"
#include "ui/MainPage.h"
#include "ui/PendingRender.h"
#include "utils/TextSlot.h"
#include <cstdio>
#include <atomic>
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

    int alignment;

    std::string label;
    std::string unit;
    int round;

    int page = 1;

    DisplayItem() : DisplayItem(0, 0, 100, 0, 0, 0, "", "", 0, 1, {}) {}

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
    //    icon. Longer text would not fit on screen anyway (rendering
    //    takes one label per span plus a transient copy of the text).
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

    // Icon left of the value (config "icon" / "iconsize", MQTT
    // DISPLAY_ICON, JS setIcon). Allocated only for items that have one;
    // its image object only while the box exists and an icon is shown. Its
    // tint is the "icon" colour of the item's styles (ui_box_icon_color).
    IconHolder icon_;
    int configuredIconSize_ = icon::kDisplayDefaultSize;

    // Which texts a script wrote last (kScript* bits). A script setter sets
    // the bit after it stored the text, a message on the item's own config
    // topic clears it (SlotReset.h): the slots are shared, and only a text
    // from a script is dropped when the script engine restarts
    // (restoreConfigState()).
    static constexpr uint8_t kScriptValue = 1;
    static constexpr uint8_t kScriptCaption = 2;
    static constexpr uint8_t kScriptUnit = 4;
    static constexpr uint8_t kScriptIcon = 8;
    std::atomic<uint8_t> scriptWrote_{0};
    SlotOwner slotOwner() const { return {"DisplayItem", id}; }

    // Config "style" (styleref::store): the item's style list. Written at
    // config load only; for an item a script made, the list it was created
    // with.
    uint8_t configStyle_[STYLE_ITEM_MAX]{};

    // Config "x" / "y" / "page", next to the run-time x / y / page a script
    // changes (setPosition, setPage), and the item's place among the config
    // items (-1: a script made it). Written at config load only.
    int16_t configX_ = 0;
    int16_t configY_ = 0;
    int16_t configPage_ = 1;
    int16_t configOrder_ = -1;

    // A script removed this config item: it is kept (ConfigManager), not
    // shown and not found, until the script engine restarts. Its config
    // topics stay subscribed, so it comes back with current values.
    std::atomic<bool> parked_{false};
    // The item's styles now (ui/ui_style.h list): the configured ones until
    // a script calls setStyles() (any task). styleDirty_ has the LVGL task
    // redraw.
    std::atomic<uint32_t> style_{0};
    std::atomic<bool> styleDirty_{false};
    // The configured label / unit were not drawn (no LVGL pool then): draw
    // the box again at the next render. LVGL task only.
    bool redraw_ = false;
    // "icon" / "iconsize", and the state for a DISPLAY_ICON
    // topic (DisplayItem.cpp). After the topics were parsed.
    void configureIcon(const JsonObject& data);
    // LVGL lock held, box exists: shows dsc as the value's icon.
    bool showIcon(IconState& s, const lv_image_dsc_t* dsc);

    // Unlinks and deletes the box (destructor). Takes the LVGL lock.
    void detach();
    // LVGL lock held.
    bool buildBox();
    void deleteBox();
    void drawConfigured();  // box exists

public:

    // styles: the item's style list (a script's display.create; config
    // items get theirs in configure()).
    DisplayItem(int x, int y, int width, int boxtype, style_list_t styles, int alignment,
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
        styleref::write(obj, configStyle_);
        obj.Add("align", alignment);
        obj.Add("width", width);
        obj.Add("label", label);
        obj.Add("unit", unit);
        obj.Add("round", round);
        if (const IconState* s = icon_.get()) {
            char name[TextSlot<IconState::kSlotBytes>::kBufferSize];
            if (s->name.read(name) && name[0] != '\0') {
                obj.Add("icon", static_cast<const char*>(name));
            }
            obj.Add("iconsize", static_cast<int>(s->size));
        }

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
        const style_list_t list = styleref::parse(data["style"], TAG);
        styleref::store(configStyle_, list);
        style_.store(list, std::memory_order_release);

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

        configureIcon(data);
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

    // Icon name ("" hides it). Any task, like the text setters.
    using SetIconResult = freebutton::SetIconResult;
    SetIconResult setIconName(std::string_view name);

    // The item's style list: refs the caller resolved (ui/ui_style.h). Any
    // task: the LVGL task redraws.
    void setStyles(style_list_t list);
    // One style (a ref, 0 = none: nothing changes) as the last of the
    // list, or out of it; any task. addStyle: false, and nothing changed,
    // when it would be a fifth style.
    bool addStyle(uint8_t ref);
    void removeStyle(uint8_t ref);
    // The list now, and the configured one (see configStyle_).
    style_list_t styles() const { return style_.load(std::memory_order_acquire); }
    style_list_t configStyles() const { return styleref::load(configStyle_); }

    // Page (config "page"; 0 = every page). Any task: the LVGL task hides or
    // shows the box on its next tick (see ui/MainPage.h).
    void setPage(int newPage);

    // Config load (ConfigManager), after configure(): the item is the
    // order-th config item; its x / y / page now are the configured ones.
    void markFromConfig(int order);
    bool fromConfig() const { return configOrder_ >= 0; }
    int configOrder() const { return configOrder_; }

    // A script removed this config item (script code): its box goes on the
    // LVGL task's next tick. restoreConfigState() shows it again.
    void park();

    // Script engine restart (ConfigManager::restoreConfigState): everything a
    // script changed goes back to the config: texts and icon a script set
    // (not those from the item's config topics), the style list,
    // position, page, and a parked item is shown again. LVGL lock held, no script code running;
    // the LVGL task redraws on its next tick. The caller requests the render.
    void restoreConfigState();

    // PendingRender (LVGL task, LVGL lock held)
    bool applyPending() override;
    bool shownNow() const override {
        return !parked_.load(std::memory_order_acquire) && ui::visibleOnPage(page, ui::shownPage());
    }

};

} // freebutton

#endif //FREEBUTTON_DISPLAYITEM_H