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
#include "FontRef.h"
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

    // Config "font" / "fontsize" (bottom label) and "topfont" /
    // "topfontsize" (top label): the local layer over the key.label /
    // key.toplabel styles (ui/ui_style.h). FONT_FACE_NONE and 0 = the
    // style's (the shared style: no per-button style).
    int fontFace_ = FONT_FACE_NONE;
    int fontSize_ = 0;
    int topFontFace_ = FONT_FACE_NONE;
    int topFontSize_ = 0;
    // The fonts now (fontref::pack): the configured ones until a script
    // calls setFont() / setTopFont() (any task); applied by the LVGL task.
    std::atomic<uint32_t> labelFont_{fontref::pack(FONT_FACE_NONE, 0)};
    std::atomic<uint32_t> topLabelFont_{fontref::pack(FONT_FACE_NONE, 0)};
    std::atomic<bool> fontDirty_{false};
    // LVGL lock held, button exists.
    void applyFonts();

    // Button position and device information
    uint32_t position_{0};
    uint8_t page_{1};
    uint8_t brokerId_{0};

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

    // Font of the bottom / top label (a fonts.h face, FONT_FACE_NONE = the
    // style's face; size 1..5 = step, else px, 0 = the style's). Any task:
    // the LVGL task applies it.
    void setLabelFont(int face, int size);
    void setTopLabelFont(int face, int size);
    // Back to the configured font and size of the label (top: the top
    // label). Any task: the configured values are not written after boot.
    void restoreFont(bool top) {
        if (top) {
            setTopLabelFont(topFontFace_, topFontSize_);
        } else {
            setLabelFont(fontFace_, fontSize_);
        }
    }

    // Script engine restart (ConfigManager::restoreConfigState): label, top
    // label and icon a script set (not those from the key's config topics)
    // and the fonts go back to the config. LVGL lock held, no script code
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
        obj.Add("id", id);
        obj.Add("position", position_);
        obj.Add("buttonuid", std::to_string(position_) + "-" + std::to_string(page_)); // not used
        obj.Add("page", page_);
        obj.Add("label", label);
        obj.Add("toplabel", topLabel);  // the key configure() reads (Button+ format)
        if (const IconState* s = icon_.get()) {
            char name[TextSlot<IconState::kSlotBytes>::kBufferSize];
            if (s->name.read(name) && name[0] != '\0') {
                obj.Add("icon", static_cast<const char*>(name));
            }
            obj.Add("iconsize", static_cast<int>(s->size));
        }
        if (fontFace_ != FONT_FACE_NONE) {
            obj.Add("font", fonts_face_name(fontFace_));
        }
        if (fontSize_ != 0) {
            obj.Add("fontsize", fontSize_);
        }
        if (topFontFace_ != FONT_FACE_NONE) {
            obj.Add("topfont", fonts_face_name(topFontFace_));
        }
        if (topFontSize_ != 0) {
            obj.Add("topfontsize", topFontSize_);
        }

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
        // Legacy fields for backwards compatibility
        if (!data["id"].isNull()) {
            id = data["id"].as<unsigned long>();
        }

        // ignoring buttonid not used in freebutton
        data["position"].isNull() ? position_ = id : position_ = data["position"].as<uint32_t>();
        data["page"].isNull() ? page_ = 1 : page_ = data["page"].as<uint32_t>();

        label = data["label"].isNull() ? label : data["label"].as<std::string>();
        // "toplabel" (Button+ format); "topLabel" is what /config wrote before
        // and what configs saved from it (data/config.json) still contain.
        if (!data["toplabel"].isNull()) {
            topLabel = data["toplabel"].as<std::string>();
        } else if (!data["topLabel"].isNull()) {
            topLabel = data["topLabel"].as<std::string>();
        }
        ledColorFront = data["ledcolorfront"].isNull() ? ledColorFront : data["ledcolorfront"].as<uint32_t>();
        ledColorWall = data["ledcolorwall"].isNull() ? ledColorWall : data["ledcolorwall"].as<uint32_t>();

        // Set device ID and broker ID - use external deviceId from main.cpp
        brokerId_ = 0; // Default broker ID

        // Parse LED topics for backwards compatibility (but don't create LEDs here)
        if (data["leds"].is<JsonArray>()) {
            for (const auto& ledJson : data["leds"].as<JsonArray>()) {
                // Add LED topics to main topics list (handled by TopicHandler)
                if (ledJson["topics"].is<JsonArray>()) {
                    handleTopics(ledJson["topics"].as<JsonArray>());  // Existing TopicHandler logic
                }
            }
        }

        // Parse button topics (existing logic)
        if (data["topics"].is<JsonArray>()) {
            handleTopics(data["topics"].as<JsonArray>());
        } else {
            // Normal for keys whose logic a script owns.
            ESP_LOGD(TAG, "Button at position %d has no topics", position_);
        }

        configureIcon(data);

        fontFace_ = fontref::parse(data["font"], TAG, "font");
        fontSize_ = data["fontsize"] | 0;
        topFontFace_ = fontref::parse(data["topfont"], TAG, "topfont");
        topFontSize_ = data["topfontsize"] | 0;
        labelFont_.store(fontref::pack(fontFace_, fontSize_), std::memory_order_release);
        topLabelFont_.store(fontref::pack(topFontFace_, topFontSize_), std::memory_order_release);

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