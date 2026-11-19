//
// Button - Example button component using TopicHandler
// Created by Bolke de Bruin on 19/11/2025.
//

#ifndef FREEBUTTON_BUTTON_H
#define FREEBUTTON_BUTTON_H

#include "mqtt/Component.h"
#include "types.h"
#include "TopicHandler.h"
#include "ButtonEventPlan.h"
#include "IconRef.h"
#include "StyleRef.h"
#include "ui/PendingRender.h"
#include "utils/TextSlot.h"
#include <atomic>
#include <string>
#include <vector>
#include <memory>

#include "ArduinoJson/Object/JsonObject.hpp"
#include "json/JsonWriter.h"

extern "C" {
#include "ui/ui_button.h"
}

namespace freebutton {

class Button final : public MqttSubscriber, public MqttPublisher, public TopicHandler<Button>, public ui::PendingRender {
public:
    static unsigned long next_id;
    unsigned long id = 0;

    std::string label;
    std::string topLabel;
    uint32_t ledColorFront;
    uint32_t ledColorWall;

    Button() : Button("", "", 0, 0, {}) {}

    Button(std::string  label, std::string  topLabel,
           uint32_t ledColorFront, uint32_t ledColorWall,
           std::vector<Topic> topicConfigs);

    ~Button();

private:
    lv_obj_t* button = nullptr;  // UI button element; written only with the LVGL lock held

    // Label / top label set from other tasks (MQTT, JS), rendered by the LVGL
    // task in applyPending(). 96 B footprint each (90 text bytes, see
    // TextSlot.h). The mini panel is 160 px wide: the label (32 px font,
    // wraps) shows ~9 glyphs per line and the top label (18 px) ~16, so the
    // text itself is ~26 bytes; the rest is room for text markup
    // (ui_button.h: a colour is 8 bytes, "\cRRGGBB", an MDI icon 4).
    static constexpr size_t kLabelSlotBytes = 96;
    static constexpr size_t kTopLabelSlotBytes = 96;
    TextSlot<kLabelSlotBytes> labelSlot_;
    TextSlot<kTopLabelSlotBytes> topLabelSlot_;

    void invalidatePending();

    // Which texts a script wrote last (kScript* bits): set by the script
    // setters, cleared by a message on the key's own config topic
    // (SlotReset.h). Only a text from a script is dropped when the script
    // engine restarts (restoreConfigState()).
    static constexpr uint8_t kScriptLabel = 1;
    static constexpr uint8_t kScriptTopLabel = 2;
    static constexpr uint8_t kScriptIcon = 4;
    std::atomic<uint8_t> scriptWrote_{0};
    SlotOwner slotOwner() const { return {"Button", id}; }
    // kScriptLabel / kScriptTopLabel: draw the configured text again in the
    // next applyPending() (its slot was reset). LVGL lock held.
    uint8_t redrawConfigured_ = 0;

    // Icon in the bottom area instead of the label (config "icon" /
    // "iconsize", MQTT ICON, JS setIcon). Allocated only for keys that have
    // one; the name slot is the runtime state like the label slots.
    IconHolder icon_;
    int configuredIconSize_ = icon::kButtonDefaultSize;

    // The button's styles now (ui/ui_style.h list): the configured ones
    // until a script calls setStyles() (any task). The bottom label carries
    // their main blocks and the top label their "title" blocks; the LVGL
    // task applies them when styleDirty_ is set.
    std::atomic<uint32_t> style_{0};
    std::atomic<bool> styleDirty_{false};
    // LVGL lock held, button exists. The styles, the labels' base fonts and
    // line heights, and a local font where the shared styles give none.
    // true when a label's styles changed: the caller draws the texts again.
    bool applyStyles();
    // LVGL lock held, button exists: the texts and the icon as they are
    // now, drawn again (a row of spans keeps the colour and font it was
    // rendered with; the icon its tint).
    void redrawTexts();

    // Button position and device information
    uint32_t position_{0};
    uint8_t page_{1};
    uint8_t brokerId_{0};
    // Config "style" (styleref::store). Written at config load only.
    uint8_t configStyle_[STYLE_ITEM_MAX]{};

public:

    // MqttComponent interface
    void onMessage(uint8_t brokerId, std::string_view topic, std::string_view data) override;
    const std::string getComponentType() const override;

    // What the button shows: the text last set at runtime (MQTT, JS), else
    // the configured label. Any task. out must hold kLabelTextBytes.
    static constexpr size_t kLabelTextBytes = TextSlot<kLabelSlotBytes>::kBufferSize;
    static_assert(kLabelSlotBytes == kTopLabelSlotBytes, "label getters share one buffer size");
    void currentLabel(char (&out)[kLabelTextBytes]) const;
    void currentTopLabel(char (&out)[kLabelTextBytes]) const;

    // Button-specific methods
    void click() const;
    void longPress() const;
    void release() const;

    // Position and event handling
    uint32_t getPosition() const { return position_; }
    void handlePhysicalEvent(EventType event);

    // How this key is exposed to Home Assistant (event entity, event_types,
    // canonical-topic mirroring). Shared by discovery and the press publisher.
    ha::ButtonEventPlan homeAssistantPlan(uint8_t haBrokerId) const;

    // UI button access for scripting
    lv_obj_t* getButtonObject() const { return button; }

    // Programmatic updates (JS). Any task: they only store the text; the
    // LVGL task renders it on its next tick. No LVGL lock is taken.
    void setLabelText(std::string_view text);
    void setTopLabelText(std::string_view text);

    // Icon name ("" hides the icon and shows the label). Any task, like the
    // label setters. Invalid: not an icon name; NoMemory: the heap could not
    // hold the icon state (only on the first icon of a key).
    using SetIconResult = freebutton::SetIconResult;
    SetIconResult setIconName(std::string_view name);

    // The button's style list: refs the caller resolved (ui/ui_style.h).
    // Any task: the LVGL task applies it.
    void setStyles(style_list_t list);
    // One style (a ref, 0 = none: nothing changes) as the last of the
    // list, or out of it; any task. addStyle: false, and nothing changed,
    // when it would be a fifth style.
    bool addStyle(uint8_t ref);
    void removeStyle(uint8_t ref);
    // The list now, and the one config.json gives the button.
    style_list_t styles() const { return style_.load(std::memory_order_acquire); }
    style_list_t configStyles() const { return styleref::load(configStyle_); }

    // Script engine restart (ConfigManager::restoreConfigState): label, top
    // label and icon a script set (not those from the key's config topics),
    // and the style list go back to the config. LVGL lock held, no script code
    // running; the LVGL task draws on its next tick. The caller requests the
    // render.
    void restoreConfigState();

    // PendingRender (LVGL task, LVGL lock held)
    bool applyPending() override;

    static std::unique_ptr<Button> fromJson(const JsonObject& config) {
        auto button = std::make_unique<Button>();
        button->configure(config);
        button->createButton();   // Create UI button after configuration
        return button;
    }

    template<typename OutputStream>
    void toJson(json::JsonWriter<OutputStream>::ScopedObject &obj) {
        obj.Add("position", position_);
        obj.Add("page", page_);
        obj.Add("label", label);
        obj.Add("toplabel", topLabel);
        if (const IconState* s = icon_.get()) {
            char name[TextSlot<IconState::kSlotBytes>::kBufferSize];
            if (s->name.read(name) && name[0] != '\0') {
                obj.Add("icon", static_cast<const char*>(name));
            }
            obj.Add("iconsize", static_cast<int>(s->size));
        }
        styleref::write(obj, configStyle_);

        // topics array
        {
            auto arr = obj.Array("topics");
            for (auto &topic: topics) {
                auto item = arr.Object();
                // tp.second.eventType = tp.first;
                topic.toJson<OutputStream>(item);
            }
        }
    }

private:
    static constexpr auto TAG = "Button";
    std::vector<Topic> topics;

    void publishToTopic(EventType eventType, const std::string& payload) const;
    // "icon" / "iconsize", and the icon state for an ICON topic (Button.cpp).
    void configureIcon(const JsonObject& data);

    // Configuration from JSON
    void configure(const JsonObject& data) {
        // The position says which key this is. An entry without one is
        // not loaded at all (ConfigManager::loadButtons).
        position_ = data["position"].as<uint32_t>();
        data["page"].isNull() ? page_ = 1 : page_ = data["page"].as<uint32_t>();

        label = data["label"].isNull() ? label : data["label"].as<std::string>();
        topLabel = data["toplabel"].isNull() ? topLabel : data["toplabel"].as<std::string>();
        ledColorFront = data["ledcolorfront"].isNull() ? ledColorFront : data["ledcolorfront"].as<uint32_t>();
        ledColorWall = data["ledcolorwall"].isNull() ? ledColorWall : data["ledcolorwall"].as<uint32_t>();

        // Set device ID and broker ID - use external deviceId from main.cpp
        brokerId_ = 0; // Default broker ID

        if (data["topics"].is<JsonArray>()) {
            handleTopics(data["topics"].as<JsonArray>());
        } else {
            // Normal for keys whose logic a script owns.
            ESP_LOGD(TAG, "Button at position %d has no topics", position_);
        }

        configureIcon(data);

        const style_list_t list = styleref::parse(data["style"], TAG);
        styleref::store(configStyle_, list);
        style_.store(list, std::memory_order_release);

        ESP_LOGI(TAG, "Configured Button %d at position %d", id, position_);
    }

public:
    // CRTP interface for TopicHandler
    static const char* getComponentName() { return "Button"; }
    std::vector<Topic>& getTopicsRef() { return topics; }
    void registerTopics();
    void unregisterTopics();

    // UI methods
    void createButton();
    void destroyButton();


};

} // namespace freebutton

#endif //FREEBUTTON_BUTTON_H