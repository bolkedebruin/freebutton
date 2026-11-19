//
// Button - Example button component using TopicHandler
// Created by Bolke de Bruin on 19/11/2025.
//

#include "Button.h"
#include "SlotReset.h"
#include "ButtonEventPlan.h"
#include "config.h"
#include "mqtt/Registry.h"
#include "drivers/LvglDisplay.h"
#include "drivers/DisplayManager.h"
#include "types.h"
#include "ui/ui_style.h"

extern "C" {
#include "ui/fonts/fonts.h"
}
#include <esp_log.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <span>
#include <utility>

namespace freebutton {

unsigned long Button::next_id = 1;

Button::Button(std::string label, std::string  topLabel,
               const uint32_t ledColorFront, const uint32_t ledColorWall,
               std::vector<Topic> topicConfigs)
    : id(next_id++)
    , label(std::move(label)), topLabel(std::move(topLabel))
    , ledColorFront(ledColorFront), ledColorWall(ledColorWall)
{
    topics = std::move(topicConfigs);

    // Set default values for manual button creation
    position_ = id;
    page_ = 1;
    brokerId_ = 0;

}

Button::~Button() {
    // Unregister subscriptions first: unsubscribe(this) takes the broker's
    // subscription lock, so it waits for an onMessage() running on the MQTT
    // task to finish and no new one can start. Only then destroy the UI button.
    unregisterTopics();
    destroyButton();
}

void Button::registerTopics() {
    for (const auto& topic : topics) {
        switch (topic.eventType) {
            case LABEL:
            case TOP_LABEL:
            case ICON:
            case LED_RGB:
            case LED_FRONT_RGB:
            case LED_WALL_RGB:
                addSubscription(topic.brokerId, topic.topic);
                break;

            case CLICK:
            case LONG_PRESS:
            case RELEASE:
                addPublication(topic.brokerId, topic.topic, 0, false);
                break;

            default:
                // Default to subscription for unknown types
                addSubscription(topic.brokerId, topic.topic);
                break;
        }
    }
}

// Runs in the MQTT task. Right after connect the broker sends the retained
// label and top label of every button in a burst, so this must not block: it
// only copies the payload into the button's slot and returns. The LVGL task
// renders it (applyPending), and repeated updates coalesce into one render.
void Button::onMessage(uint8_t brokerId, std::string_view topic, std::string_view data) {
    for (const auto& topicConfig : topics) {
        if (brokerId == topicConfig.brokerId && topic == topicConfig.topic) {
            ESP_LOGD(TAG, "Button %lu: topic=%.*s value=%.*s eventType=%d",
                     id, (int)topic.length(), topic.data(), (int)data.length(), data.data(), topicConfig.eventType);

            switch (topicConfig.eventType) {
                // Each also notes that the text is no longer a script's
                // (scriptWrote_): it stays when the script engine restarts.
                case LABEL:
                    storeFromConfigTopic(labelSlot_, data, slotOwner(), "label", scriptWrote_, kScriptLabel);
                    break;

                case TOP_LABEL:
                    storeFromConfigTopic(topLabelSlot_, data, slotOwner(), "top label", scriptWrote_, kScriptTopLabel);
                    break;

                case ICON:
                    // The state exists: configureIcon() made it for this topic.
                    if (IconState* s = icon_.get()) {
                        storeFromConfigTopic(s->name, data, slotOwner(), "icon", scriptWrote_, kScriptIcon);
                    }
                    break;

                case LED_RGB:
                case LED_FRONT_RGB:
                case LED_WALL_RGB:
                    // Subscribed in registerTopics() but not rendered by the
                    // button UI. Silent: every retained message lands here
                    // while the broker's subscription lock is held.
                    break;

                default:
                    ESP_LOGD(TAG, "Button %lu: Unhandled event type: %d", id, static_cast<int>(topicConfig.eventType));
                    break;
            }
            break;
        }
    }
}

namespace {
template <class Slot, size_t N>
void currentText(const Slot& slot, const std::string& configured, char (&out)[N]) {
    if (!slot.read(out)) {
        // Never set at runtime (or briefly busy): the configured text is shown.
        const size_t n = std::min(configured.size(), N - 1);
        std::memcpy(out, configured.data(), n);
        out[n] = '\0';
    }
}
} // namespace

void Button::currentLabel(char (&out)[kLabelTextBytes]) const { currentText(labelSlot_, label, out); }
void Button::currentTopLabel(char (&out)[kLabelTextBytes]) const { currentText(topLabelSlot_, topLabel, out); }

// LVGL task, LVGL lock held. Each slot hands over a stable stack copy of its
// text (at most TextSlot::kCapacity bytes), rendered once; the label copies
// it. Text markup applies (ui_button.h).
bool Button::applyPending() {
    if (!button) {
        return false;  // createButton() requests a render once the button exists
    }
    // A script changed a value of a style this button uses (fb.style.set):
    // the texts are drawn again, and the labels attached again (a block may
    // hold its first LVGL property since, ui_style.h).
    const bool changed = ui_style_changed(STYLE_KIND_BUTTON, style_.load(std::memory_order_acquire));
    const bool dirty = styleDirty_.exchange(false, std::memory_order_acq_rel);
    bool restyled = changed;
    if (dirty || changed) {
        restyled = applyStyles() || restyled;
    }
    if (restyled) {
        redrawTexts();
    }
    if (redrawConfigured_ != 0) {
        // restoreConfigState() reset these slots: the configured text again.
        if (redrawConfigured_ & kScriptTopLabel) {
            ui_button_set_title(button, topLabel.c_str());
        }
        if (redrawConfigured_ & kScriptLabel) {
            ui_button_set_text(button, label.c_str());
        }
        redrawConfigured_ = 0;
    }
    bool again = false;
    again |= labelSlot_.apply([this](const char* text) { ui_button_set_text(button, text); })
             == TextSlotApply::Busy;
    again |= topLabelSlot_.apply([this](const char* text) { ui_button_set_title(button, text); })
             == TextSlotApply::Busy;
    if (IconState* s = icon_.get()) {
        again |= s->apply([this](const lv_image_dsc_t* dsc) {
            ui_button_set_icon(button, dsc);  // nullptr: the label shows again
            return true;
        });
    }
    // Styles the LVGL pool had no room for (a label, or a row of spans
    // drawn just now): the button shows what it carried, font included, and
    // applyStyles() runs again in a later walk. Not at once: that would
    // spin while the pool stays low.
    if (ui_button_styles_pending(button)) {
        styleDirty_.store(true, std::memory_order_release);
        ui::requestPendingRenderLater();
    }
    return again;
}

// LVGL lock held, button exists. The configured label and top label were just
// drawn; render the last text received for each over them on the next tick.
void Button::invalidatePending() {
    labelSlot_.invalidate();
    topLabelSlot_.invalidate();
    if (IconState* s = icon_.get()) {
        s->name.invalidate();
    }
    linkPending();
    ui::requestPendingRender();
}

namespace {

// What a label's text starts from: the lookup (ui/ui_style.h) of its part
// through the styles the label carries. `local`: the shared styles do not
// give that font (the face comes from one style and the size from
// another), so the label needs it as a local style.
struct LabelLook {
    int face;
    int px;
    int lineHeight;  // x100, 0 = no style sets one
    bool local;
};

LabelLook labelLook(const style_list_t carried, const style_part_t part) {
    style_props_t r;
    ui_style_lookup(STYLE_KIND_BUTTON, part, carried, &r, nullptr);
    int sharedFace = 0;
    int sharedPx = 0;
    const bool shared = ui_style_shared_font(STYLE_KIND_BUTTON, part, carried, &sharedFace, &sharedPx);
    return {r.face, r.px, (r.has & STYLE_HAS_LINE_HEIGHT) ? r.line_height : 0,
            !shared || sharedFace != r.face || sharedPx != r.px};
}

} // namespace

// LVGL lock held, button exists. The styles first: the fonts follow what
// the labels carry then (styles that found no room in the LVGL pool are not
// there, and their font and size must not show without their colour).
// Returns whether the texts have to be set again: only that puts the fonts
// on the labels (a text's own markup font stays on a text that is not set
// again).
bool Button::applyStyles() {
    bool restyled = ui_button_set_styles(button, style_.load(std::memory_order_acquire));
    style_list_t carried = 0;
    style_list_t titleCarried = 0;
    ui_button_get_styles(button, &carried, &titleCarried);
    const LabelLook text = labelLook(carried, STYLE_PART_MAIN);
    const LabelLook title = labelLook(titleCarried, STYLE_PART_TITLE);
    restyled = ui_button_set_base(button, text.face, text.px, text.lineHeight, title.face, title.px,
                                  title.lineHeight) || restyled;
    restyled = ui_button_set_fonts(button, text.local ? fonts_get(text.face, text.px) : nullptr,
                                   title.local ? fonts_get(title.face, title.px) : nullptr) || restyled;
    return restyled;
}

// LVGL lock held, button exists.
void Button::redrawTexts() {
    char text[kLabelTextBytes];
    currentTopLabel(text);
    ui_button_set_title(button, text);
    currentLabel(text);
    ui_button_set_text(button, text);
    if (IconState* s = icon_.get()) {
        s->name.invalidate();
    }
}

void Button::setStyles(const style_list_t list) {
    if (style_.exchange(list, std::memory_order_acq_rel) == list) {
        return;  // the list it has: nothing to draw
    }
    styleDirty_.store(true, std::memory_order_release);
    ui::requestPendingRender();
}

bool Button::addStyle(const uint8_t ref) {
    bool changed = false;
    if (!styleref::add(style_, ref, &changed)) {
        return false;
    }
    if (changed) {
        styleDirty_.store(true, std::memory_order_release);
        ui::requestPendingRender();
    }
    return true;
}

void Button::removeStyle(const uint8_t ref) {
    bool changed = false;
    styleref::remove(style_, ref, &changed);
    if (changed) {
        styleDirty_.store(true, std::memory_order_release);
        ui::requestPendingRender();
    }
}

// JS bindings. These run in the JS task, or in an MQTT task when called from a
// JS mqtt.subscribe() callback, so like onMessage they only store the text.
void Button::setLabelText(const std::string_view text) {
    storeFromScript(labelSlot_, text, slotOwner(), "label", scriptWrote_, kScriptLabel);
}

void Button::setTopLabelText(const std::string_view text) {
    storeFromScript(topLabelSlot_, text, slotOwner(), "top label", scriptWrote_, kScriptTopLabel);
}

// LVGL lock held (so applyPending() is not running), no script code running.
// Only stores and flags: the LVGL task draws on its next tick.
void Button::restoreConfigState() {
    const uint8_t wrote = scriptWrote_.exchange(0, std::memory_order_acq_rel);
    resetIfScriptWrote(labelSlot_, wrote, kScriptLabel);
    resetIfScriptWrote(topLabelSlot_, wrote, kScriptTopLabel);
    if (button != nullptr) {
        redrawConfigured_ |= wrote & (kScriptLabel | kScriptTopLabel);
    }
    if (wrote & kScriptIcon) {
        icon_.restoreConfigName();  // "" (no config icon) shows the label again
    }
    const style_list_t configured = configStyles();
    if (style_.exchange(configured, std::memory_order_acq_rel) != configured) {
        styleDirty_.store(true, std::memory_order_release);
    }
}

// Boot (config load), before the button is created and subscribed.
void Button::configureIcon(const JsonObject& data) {
    configuredIconSize_ = icon::clampSize(data["iconsize"] | 0, icon::kButtonDefaultSize);
    const char* name = data["icon"] | "";
    bool wanted = name[0] != '\0';
    for (const auto& t : topics) {
        wanted = wanted || t.eventType == ICON;
    }
    if (!wanted) {
        return;  // no icon state: a key without icon costs one pointer
    }
    if (name[0] != '\0' && !icon::validName(name)) {
        ESP_LOGW(TAG, "Button at position %u: icon \"%s\" is not a valid icon name, ignored", position_, name);
        name = "";
    }
    IconState* s = icon_.ensure(configuredIconSize_, name);  // keeps the config name
    if (s == nullptr) {
        ESP_LOGE(TAG, "Button at position %u: no memory for its icon", position_);
        return;
    }
    if (name[0] != '\0') {
        s->name.write(name);
    }
}

// Script code (JS task). Like the label setters, only stores the name.
Button::SetIconResult Button::setIconName(const std::string_view name) {
    return setIconFromScript(icon_, name, configuredIconSize_, slotOwner(), scriptWrote_, kScriptIcon);
}

const std::string Button::getComponentType() const {
    return "button";
}

void Button::click() const {
    publishToTopic(CLICK, CLICK_PAYLOAD);
    ESP_LOGI(TAG, "Button %lu clicked", id);
}

void Button::longPress() const {
    publishToTopic(LONG_PRESS, LONG_PRESS_PAYLOAD);
    ESP_LOGI(TAG, "Button %lu long pressed", id);
}

ha::ButtonEventPlan Button::homeAssistantPlan(const uint8_t haBrokerId) const {
    // No heap (runs on the Home Assistant discovery task): the plan only
    // looks at the FIRST topic per press type, so pass just those.
    std::array<ha::PressTopic, 3> pressTopics{};
    size_t count = 0;
    for (const auto& t : topics) {
        if (t.eventType != CLICK && t.eventType != LONG_PRESS && t.eventType != RELEASE) {
            continue;
        }
        bool seen = false;
        for (size_t i = 0; i < count; ++i) {
            seen = seen || pressTopics[i].eventType == t.eventType;
        }
        if (!seen) {
            pressTopics[count++] = {t.brokerId, t.topic, t.payload, t.eventType};
            if (count == pressTopics.size()) break;
        }
    }
    char canonical[96];
    const size_t canonicalLength =
        ha::canonicalButtonStateTopic(canonical, sizeof(canonical), deviceId, static_cast<int>(position_));
    if (canonicalLength == 0) {
        return ha::unannouncedPlan("canonical press topic does not fit its buffer");
    }
    return ha::planButtonEvents(std::span<const ha::PressTopic>(pressTopics.data(), count), haBrokerId,
                                std::string_view(canonical, canonicalLength));
}

void Button::release() const {
    publishToTopic(RELEASE, RELEASE_PAYLOAD);
    ESP_LOGI(TAG, "Button %lu released", id);
}

void Button::handlePhysicalEvent(EventType event) {
    ESP_LOGI(TAG, "Button %lu handling physical event: %d", id, static_cast<int>(event));

    switch (event) {
        case CLICK:
            click();
            break;
        case LONG_PRESS:
            longPress();
            break;
        case RELEASE:
            release();
            break;
        default:
            ESP_LOGW(TAG, "Button %lu: Unknown physical event: %d", id, static_cast<int>(event));
            break;
    }
}

void Button::unregisterTopics() {
    // Clear all current subscriptions and publications using the new clear methods
    clearSubscriptions();
    clearPublications();
}

// handleTopics is now automatically provided by TopicHandler template

void Button::publishToTopic(const EventType eventType, const std::string& payload) const {
    for (const auto& topic : topics) {
        if (topic.eventType == eventType) {
            const std::string& actualPayload = topic.payload.empty() ? payload : topic.payload;
            publish(topic.brokerId, topic.topic, actualPayload, 0, false);

            ESP_LOGD(TAG, "Button %lu published to topic %s: %s",
                    id, topic.topic.c_str(), actualPayload.c_str());
            break;
        }
    }
}

void Button::createButton() {
    // Get the appropriate small display for this button
    // Button position maps to physical button, which should map to physical display
    auto& lvglDisplay = FreeButton::getLvglDisplay();
    auto& displayManager = FreeButton::getDisplayManager();

    // Map button position to small display index
    int smallDisplayIndex = displayManager.getSmallDisplayIndexByPosition(position_);

    if (smallDisplayIndex < 0) {
        ESP_LOGD(TAG, "Button position %u has no small display (likely on large display connector)", position_);
        return;
    }

    lv_display_t* smallDisplay = lvglDisplay.getSmallDisplay(smallDisplayIndex);

    if (!smallDisplay) {
        ESP_LOGD(TAG, "Small display %d not available for Button position %u, skipping UI creation",
                 smallDisplayIndex, position_);
        return;
    }

    // Lock first: everything below is LVGL state the LVGL task also uses.
    // lv_display_set_default() is global and lv_scr_act() in the LVGL task
    // (e.g. the device info modal) reads it, so it must only change, and be
    // restored, while the lock is held.
    lvglDisplay.lock();

    // Acquire buffer for the small display. It waits at most 100 ms for the
    // shared-buffer mutex, whose holders never wait for the LVGL lock, so
    // holding the LVGL lock here cannot deadlock.
    if (!lvglDisplay.acquireSmallDisplayBuffer(smallDisplayIndex)) {
        ESP_LOGW(TAG, "Failed to acquire buffer for small display %d, Button position %u",
                 smallDisplayIndex, position_);
        lvglDisplay.unlock();
        return;
    }

    // Save current default display and temporarily set to small display
    lv_display_t* original_default_disp = lv_display_get_default();
    lv_display_set_default(smallDisplay);

    // Every exit after this point must release the buffer, restore the
    // default display and unlock, in that order.
    auto finish = [&]() {
        lvglDisplay.releaseSmallDisplayBuffer(smallDisplayIndex);
        lv_display_set_default(original_default_disp);
        lvglDisplay.unlock();
    };

    // Get the screen from the small display
    lv_obj_t* parent = lv_display_get_screen_active(smallDisplay);

    if (!parent) {
        ESP_LOGE(TAG, "Screen not available for Button position %u on small display %d", position_, smallDisplayIndex);
        finish();
        return;
    }

    // If button already exists, just redraw it with new configuration
    if (button != nullptr) {
        ESP_LOGI(TAG, "Redrawing existing button for Button %lu", id);
        styleDirty_.store(false, std::memory_order_release);
        applyStyles();
        ui_button_set_title(button, topLabel.c_str());
        ui_button_set_text(button, label.c_str());
        invalidatePending();
        finish();
        return;
    }

    // Initialize button styles if not already done (should be done once globally)
    static bool styles_initialized = false;
    if (!styles_initialized) {
        ui_button_styles();
        styles_initialized = true;
    }

    // Create the button with the correct small display parent
    button = ui_button_create(parent);
    if (!button) {
        ESP_LOGE(TAG, "Failed to create UI button for Button position %u", position_);
        finish();
        return;
    }

    // Apply the initial configuration
    styleDirty_.store(false, std::memory_order_release);
    applyStyles();
    ui_button_set_title(button, topLabel.c_str());
    ui_button_set_text(button, label.c_str());
    invalidatePending();

    // Force refresh the small display
    lv_obj_invalidate(parent);

    ESP_LOGI(TAG, "Created UI button for position %u (Button id %lu) on small display %d",
             position_, id, smallDisplayIndex);

    finish();
}

void Button::destroyButton() {
    if (button == nullptr) {
        return;
    }

    // Lock screen access for thread safety
    auto& lvglDisplay = FreeButton::getLvglDisplay();
    lvglDisplay.lock();

    // Off the LVGL task's list before the button (and later this object) goes.
    unlinkPending();
    ui_button_delete(button);  // with its descriptor (lv_obj_del() leaked it)
    button = nullptr;
    if (IconState* s = icon_.get()) {
        s->raster.release();  // the image object went with the button
    }
    ESP_LOGI(TAG, "Destroyed UI button for Button %lu", id);

    lvglDisplay.unlock();
}


} // namespace freebutton
