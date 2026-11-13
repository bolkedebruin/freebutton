//
// Created by Bolke de Bruin on 18/11/2025.
//

#include "DisplayItem.h"
#include "mqtt/Registry.h"
#include "drivers/LvglDisplay.h"
#include "ui/LvglRoom.h"
#include "ui/MainPage.h"
#include "types.h"

#include <utility>

namespace freebutton {

unsigned long DisplayItem::next_id = 1;

namespace {
// LVGL pool an icon's image object takes (lv_image_t plus two local style
// properties), with margin.
constexpr size_t kRoomForIcon = 512;
} // namespace

DisplayItem::DisplayItem(const int x, const int y, const int width, const int boxtype, const int fontSize, const int alignment,
                        std::string  label, std::string  unit, const int round, const int page,
                        std::vector<Topic> topicConfigs)
    : id(next_id++)
    , x(x), y(y), width(width), boxType(boxtype)
    , fontSize(fontSize), alignment(alignment)
    , label(std::move(label)), unit(std::move(unit)), round(round), page(page)
{
    topics = std::move(topicConfigs);
    font_.store(fontref::pack(FONT_FACE_NONE, fontSize), std::memory_order_relaxed);
    // Topics will be registered later via ConfigManager::registerAllSubscriptions()
    // UI box creation is deferred until after configuration (called from fromJson)
}

DisplayItem::~DisplayItem() {
    // Unregister subscriptions first: unsubscribe(this) takes the broker's
    // subscription lock, so it waits for an onMessage() running on the MQTT
    // task to finish and no new one can start. Only then destroy the UI box.
    unregisterTopics();
    detach();
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
                case DISPLAY_ICON:
                    // The state exists: configureIcon() made it for this topic.
                    if (IconState* s = icon_.get()) {
                        storeText(s->name, data, id, "icon");
                    }
                    break;
                default:
                    // Debug only: runs under the broker's subscription lock.
                    ESP_LOGD(TAG, "DisplayItem %lu: Unhandled event type: %d", id, topicConfig.eventType);
                    break;
            }
            break;
        }
    }
}

// LVGL task, LVGL lock held. Builds the box when the page is shown and
// deletes it when the page goes; while shown, renders the newest text of each
// field. Each slot hands over a stable stack copy of its text (at most 59
// bytes), rendered once. Runs on every render request of any widget, so the
// hidden case is one page check.
bool DisplayItem::applyPending() {
    if (!ui::visibleOnPage(page, ui::shownPage())) {
        deleteBox();
        return false;
    }
    // A box built now draws with the current font anyway.
    const bool fontChanged = fontDirty_.exchange(false, std::memory_order_acq_rel);
    if (box == nullptr) {
        if (!buildBox()) {
            return false;  // no LVGL memory now; the next render request retries
        }
    } else if (fontChanged) {
        drawConfigured();  // the new font; the slots below render over it
    }
    bool again = false;
    again |= valueSlot_.apply([this](const char* text) { ui_box_set_text(box, text); })
             == TextSlotApply::Busy;
    again |= captionSlot_.apply([this](const char* text) { ui_box_set_caption(box, text); })
             == TextSlotApply::Busy;
    again |= unitSlot_.apply([this](const char* text) { ui_box_set_hint(box, text); })
             == TextSlotApply::Busy;
    if (IconState* s = icon_.get()) {
        again |= s->apply([this, s](const lv_image_dsc_t* dsc) { return showIcon(*s, dsc); });
    }
    return again;
}

// LVGL lock held, box exists.
bool DisplayItem::showIcon(IconState& s, const lv_image_dsc_t* dsc) {
    if (dsc != nullptr && !ui_box_has_icon(box) && !ui::lvglHasRoom(kRoomForIcon)) {
        if (!s.roomWarned) {
            ESP_LOGE(TAG, "DisplayItem %lu: no LVGL memory for its icon", id);
            s.roomWarned = true;
        }
        return false;  // shown without icon; the next change of icon retries
    }
    const lv_color_t tint = s.color != icon::kNoColor ? lv_color_hex(s.color) : ui_box_text_color(box);
    return ui_box_set_icon(box, dsc, tint) && dsc != nullptr;
}

// LVGL lock held, box exists. Draws the configured label and unit and an
// empty value, then has the next apply render the last text received for
// each field over them (a slot never written keeps the configured default).
void DisplayItem::drawConfigured() {
    const int adjustedWidth = width == 0 ? 45 : width;
    const uint32_t font = font_.load(std::memory_order_acquire);
    const auto clampSize = [](const int size) { return static_cast<uint8_t>(size < 0 ? 0 : size > 255 ? 255 : size); };
    const ui_box_fonts_t fonts = {
        .face = static_cast<int16_t>(fontref::face(font)),
        .fontsize = clampSize(fontref::size(font)),
        .caption_face = static_cast<int16_t>(labelFontFace),
        .caption_fontsize = clampSize(labelFontSize),
    };
    draw_box(box, label.c_str(), "", unit.c_str(), x, y, &fonts, adjustedWidth, alignment, boxType);
    valueSlot_.invalidate();
    captionSlot_.invalidate();
    unitSlot_.invalidate();
    if (IconState* s = icon_.get()) {
        s->name.invalidate();
    }
}

// LVGL lock held.
bool DisplayItem::buildBox() {
    lv_obj_t* parent = FreeButton::getLvglDisplay().getMainScreen();
    if (parent == nullptr) {
        return false;
    }
    if (!ui::lvglHasRoom(ui::kRoomForBox)) {
        if (!buildFailed_) {
            ESP_LOGE(TAG, "DisplayItem %lu: no LVGL memory to show it", id);
            buildFailed_ = true;
        }
        return false;
    }
    static bool styles_initialized = false;  // shared by all boxes
    if (!styles_initialized) {
        ui_box_create_styles();
        styles_initialized = true;
    }
    box = ui_box_create(parent);
    if (box == nullptr) {
        ESP_LOGE(TAG, "DisplayItem %lu: no LVGL memory for its box", id);
        buildFailed_ = true;
        return false;
    }
    buildFailed_ = false;
    drawConfigured();
    return true;
}

// LVGL lock held.
void DisplayItem::deleteBox() {
    if (box != nullptr) {
        ui_box_delete(box);  // with its icon's image object
        box = nullptr;
        if (IconState* s = icon_.get()) {
            s->raster.release();
        }
    }
}

void DisplayItem::setPage(const int newPage) {
    page = newPage;
    ui::requestPendingRender();
}

const std::string DisplayItem::getComponentType() const {
    return "display_item";
}

void DisplayItem::unregisterTopics() {
    // Clear all current subscriptions using the new clearSubscriptions method
    clearSubscriptions();
}

void DisplayItem::attach() {
    auto& lvglDisplay = FreeButton::getLvglDisplay();
    lvglDisplay.lock();
    linkPending();
    if (box != nullptr) {
        drawConfigured();  // configuration changed: redraw the shown box
    }
    lvglDisplay.unlock();
    ui::requestPendingRender();  // builds the box if its page is shown
}

void DisplayItem::detach() {
    auto& lvglDisplay = FreeButton::getLvglDisplay();
    lvglDisplay.lock();
    // Off the LVGL task's list before the box (and later this item) goes.
    unlinkPending();
    deleteBox();
    lvglDisplay.unlock();
}

// JS bindings (loop or button task, JS engine lock held). Like onMessage they
// only store the text.
void DisplayItem::setDisplayLabel(const std::string_view text) {
    storeText(captionSlot_, text, id, "caption");
}

void DisplayItem::setDisplayValue(const std::string_view text) {
    storeText(valueSlot_, text, id, "value");
}

void DisplayItem::setDisplayUnit(const std::string_view text) {
    storeText(unitSlot_, text, id, "unit");
}

void DisplayItem::setFont(const int face, const int size) {
    font_.store(fontref::pack(face, size), std::memory_order_release);
    fontDirty_.store(true, std::memory_order_release);
    ui::requestPendingRender();
}

// Boot (config load), before the item is attached and subscribed.
void DisplayItem::configureIcon(const JsonObject& data) {
    configuredIconSize_ = icon::clampSize(data["iconsize"] | 0, icon::kDisplayDefaultSize);
    configuredIconColor_ = icon::parseColor(data["iconcolor"] | "");
    const char* name = data["icon"] | "";
    bool wanted = name[0] != '\0';
    for (const auto& t : topics) {
        wanted = wanted || t.eventType == DISPLAY_ICON;
    }
    if (!wanted) {
        return;  // no icon state: an item without icon costs one pointer
    }
    if (name[0] != '\0' && !icon::validName(name)) {
        ESP_LOGW(TAG, "DisplayItem %lu: icon \"%s\" is not a valid icon name, ignored", id, name);
        name = "";
    }
    IconState* s = ensureIcon();
    if (s == nullptr) {
        ESP_LOGE(TAG, "DisplayItem %lu: no memory for its icon", id);
        return;
    }
    if (name[0] != '\0') {
        s->name.write(name);
    }
}

DisplayItem::SetIconResult DisplayItem::setIconName(const std::string_view name) {
    if (!name.empty() && !icon::validName(name)) {
        return SetIconResult::Invalid;
    }
    IconState* s = icon_.get();
    if (s == nullptr) {
        if (name.empty()) {
            return SetIconResult::Ok;  // no icon to hide
        }
        s = ensureIcon();
        if (s == nullptr) {
            return SetIconResult::NoMemory;
        }
    }
    storeText(s->name, name, id, "icon");
    return SetIconResult::Ok;
}

} // freebutton
