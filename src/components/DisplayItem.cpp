//
// Created by Bolke de Bruin on 18/11/2025.
//

#include "DisplayItem.h"
#include "mqtt/Registry.h"
#include "drivers/LvglDisplay.h"
#include "types.h"

#include <utility>

namespace freebutton {

unsigned long DisplayItem::next_id = 1;

DisplayItem::DisplayItem(const int x, const int y, const int width, const int boxtype, const int fontSize, const int alignment,
                        std::string  label, std::string  unit, const int round, const int page,
                        std::vector<Topic> topicConfigs)
    : id(next_id++)
    , x(x), y(y), width(width), boxType(boxtype)
    , fontSize(fontSize), alignment(alignment)
    , label(std::move(label)), unit(std::move(unit)), round(round), page(page)
{
    topics = std::move(topicConfigs);
    // Topics will be registered later via ConfigManager::registerAllSubscriptions()
    // UI box creation is deferred until after configuration (called from fromJson)
}

DisplayItem::~DisplayItem() {
    // Unregister subscriptions first: unsubscribe(this) takes the broker's
    // subscription lock, so it waits for an onMessage() running on the MQTT
    // task to finish and no new one can start. Only then destroy the UI box.
    unregisterTopics();
    destroyBox();
}

namespace {

// Store text for the LVGL task. Any task; no heap, no LVGL lock.
template <size_t Bytes>
void storeText(TextSlot<Bytes>& slot, const std::string_view text, const unsigned long id, const char* field) {
    if (slot.write(text).firstTruncation) {
        ESP_LOGW("DisplayItem", "DisplayItem %lu: %s of %u bytes cut to %u (logged once per item)",
                 id, field, static_cast<unsigned>(text.size()),
                 static_cast<unsigned>(TextSlot<Bytes>::kCapacity));
    }
    ui::requestPendingRender();
}

} // namespace

void DisplayItem::registerTopics() {
    for (const auto& topic : topics) {
        addSubscription(topic.brokerId, topic.topic);
    }
}

// Runs in the MQTT task. Right after connect the broker sends one retained
// message per subscribed item in a burst, so this must not block: it only
// copies the payload into the item's slot and returns. The LVGL task renders
// it (applyPending), and repeated updates of one item coalesce into one render.
void DisplayItem::onMessage(const uint8_t brokerId, const std::string_view topic, const std::string_view data) {
    for (const auto& topicConfig : topics) {
        if (brokerId == topicConfig.brokerId && topic == topicConfig.topic) {
            ESP_LOGD(TAG, "DisplayItem %lu: topic=%.*s value=%.*s eventType=%d",
                     id, (int)topic.length(), topic.data(), (int)data.length(), data.data(), topicConfig.eventType);

            switch (topicConfig.eventType) {
                case DISPLAY_LABEL:
                    storeText(captionSlot_, data, id, "caption");
                    break;
                case DISPLAY_VALUE:
                    storeText(valueSlot_, data, id, "value");
                    break;
                case DISPLAY_UNIT:
                    storeText(unitSlot_, data, id, "unit");
                    break;
                default:
                    ESP_LOGW(TAG, "DisplayItem %lu: Unknown event type: %d", id, topicConfig.eventType);
                    break;
            }
            break;
        }
    }
}

// LVGL task, LVGL lock held. Each slot hands over a stable stack copy of its
// text (at most 59 bytes), rendered once.
bool DisplayItem::applyPending() {
    if (!box) {
        return false;  // createBox() requests a render once the box exists
    }
    bool again = false;
    again |= valueSlot_.apply([this](const char* text) { ui_box_set_text(box, text); })
             == TextSlotApply::Busy;
    again |= captionSlot_.apply([this](const char* text) { ui_box_set_caption(box, text); })
             == TextSlotApply::Busy;
    again |= unitSlot_.apply([this](const char* text) { ui_box_set_hint(box, text); })
             == TextSlotApply::Busy;
    return again;
}

// LVGL lock held, box exists. draw_box() just rendered the configured label
// and unit and an empty value; render the last text received for each field
// over them on the next tick.
void DisplayItem::invalidatePending() {
    valueSlot_.invalidate();
    captionSlot_.invalidate();
    unitSlot_.invalidate();
    linkPending();
    ui::requestPendingRender();
}

const std::string DisplayItem::getComponentType() const {
    return "display_item";
}

void DisplayItem::unregisterTopics() {
    // Clear all current subscriptions using the new clearSubscriptions method
    clearSubscriptions();
}

void DisplayItem::createBox() {
    // Get the main screen from LVGL display
    auto& lvglDisplay = FreeButton::getLvglDisplay();
    lv_obj_t* parent = lvglDisplay.getMainScreen();

    if (!parent) {
        ESP_LOGE(TAG, "Main screen not available for DisplayItem %lu", id);
        return;
    }

    // Lock screen access for thread safety
    lvglDisplay.lock();

    // If box already exists, just redraw it with new configuration
    if (box != nullptr) {
        ESP_LOGI(TAG, "Redrawing existing box for DisplayItem %lu", id);
        const int adjustedWidth = width == 0 ? 45 : width;
        draw_box(box, label.c_str(), "", unit.c_str(), x, y, fontSize, adjustedWidth, alignment, boxType);
        invalidatePending();
        lvglDisplay.unlock();
        return;
    }

    // Initialize box styles if not already done (should be done once globally)
    static bool styles_initialized = false;
    if (!styles_initialized) {
        ui_box_create_styles();
        styles_initialized = true;
    }

    // Create the box
    box = ui_box_create(parent);
    if (!box) {
        ESP_LOGE(TAG, "Failed to create UI box for DisplayItem %lu", id);
        lvglDisplay.unlock();
        return;
    }

    // Apply the initial configuration using draw_box
    const int adjustedWidth = width == 0 ? 45 : width;
    draw_box(box, label.c_str(), "", unit.c_str(), x, y, fontSize, adjustedWidth, alignment, boxType);
    invalidatePending();

    lvglDisplay.unlock();
}

void DisplayItem::destroyBox() {
    if (box == nullptr) {
        return;
    }

    // Lock screen access for thread safety
    auto& lvglDisplay = FreeButton::getLvglDisplay();
    lvglDisplay.lock();

    // Off the LVGL task's list before the box (and later this item) goes.
    unlinkPending();
    lv_obj_del(box);
    box = nullptr;
    ESP_LOGD(TAG, "Destroyed UI box for DisplayItem %lu", id);

    lvglDisplay.unlock();
}

// JS bindings. These run in the JS task, or in an MQTT task when called from a
// JS mqtt.subscribe() callback, so like onMessage they only store the text.
void DisplayItem::setDisplayLabel(const std::string_view text) {
    storeText(captionSlot_, text, id, "caption");
}

void DisplayItem::setDisplayValue(const std::string_view text) {
    storeText(valueSlot_, text, id, "value");
}

void DisplayItem::setDisplayUnit(const std::string_view text) {
    storeText(unitSlot_, text, id, "unit");
}

} // freebutton
