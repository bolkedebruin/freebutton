//
// List - see List.h.
//

#include "List.h"

#include "drivers/LvglDisplay.h"
#include "ui/LvglRoom.h"
#include "ui/MainPage.h"
#include "ui/ui_style.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>


namespace freebutton {

namespace {

// Guards every list's options_ string. Static: no heap, created on first use
// (the first list is configured at boot, before any script runs).
SemaphoreHandle_t itemsMutex() {
    static StaticSemaphore_t storage;
    static SemaphoreHandle_t handle = xSemaphoreCreateMutexStatic(&storage);
    return handle;
}

class ItemsLock {
public:
    ItemsLock() { xSemaphoreTake(itemsMutex(), portMAX_DELAY); }
    ~ItemsLock() { xSemaphoreGive(itemsMutex()); }
    ItemsLock(const ItemsLock&) = delete;
    ItemsLock& operator=(const ItemsLock&) = delete;
};

// Font and line spacing of a roller (LVGL lock held). The item layer: a
// config / script font or size is a local font (the face, else the list
// style's, at fontsize 1..5 = step (14/24/32/48/72), FONT_PX_MIN..MAX = px,
// else the style's px); without one the list style's font applies. The
// line space is a quarter of the line height, as before "style", unless the
// list style sets text_line_space.
void applyFont(lv_obj_t* roller, const uint32_t packed) {
    const int face = fontref::face(packed);
    const int size = fontref::size(packed);
    const style_props_t* r = ui_style_resolved(STYLE_EL_LIST);
    if (face == FONT_FACE_NONE && size == 0) {
        lv_obj_remove_local_style_prop(roller, LV_STYLE_TEXT_FONT, LV_PART_MAIN);
    } else {
        lv_obj_set_style_text_font(roller, fonts_get(face == FONT_FACE_NONE ? r->face : face,
                                                     style_logic_item_px(size, false, r->px)),
                                   LV_PART_MAIN);
    }
    if (r->has & STYLE_HAS_LINE_SPACE) {
        lv_obj_remove_local_style_prop(roller, LV_STYLE_TEXT_LINE_SPACE, LV_PART_MAIN);
    } else {
        const lv_font_t* font = lv_obj_get_style_text_font(roller, LV_PART_MAIN);
        lv_obj_set_style_text_line_space(roller, font->line_height / 4, LV_PART_MAIN);
    }
}

// Config "color" / "focuscolor" / "focustextcolor": kNoColor when absent;
// malformed is warned and ignored (the style's colour applies).
unsigned long configColor(const ArduinoJson::JsonObject& data, const char* field) {
    const char* hex = data[field] | "";
    if (hex[0] == '\0') {
        return List::kNoColor;
    }
    const unsigned long c = list::parseColor(hex, List::kNoColor);
    if (c == List::kNoColor) {
        ESP_LOGW("List", "List: \"%s\": \"%.16s\" is not a colour (\"#rrggbb\"), the style's is used", field, hex);
    }
    return c;
}

constexpr int kScreenWidth = 320;
constexpr int kScreenHeight = 240;
constexpr uint32_t kAnimMs = 200;
constexpr int kFocusRadius = 8;
constexpr int kTextPadding = 10;

} // namespace

List::~List() {
    // Deletes LVGL objects: takes the (non-recursive) LVGL lock, so the
    // caller must not hold it (ConfigManager::clear()).
    auto& display = FreeButton::getLvglDisplay();
    display.lock();
    if (linked_) {
        unlinkPending();
        linked_ = false;
    }
    deleteRoller();
    display.unlock();
}

std::unique_ptr<List> List::fromJson(const ArduinoJson::JsonObject& config) {
    auto l = std::make_unique<List>();
    l->configure(config);
    return l;
}

void List::configure(const ArduinoJson::JsonObject& data) {
    x_ = data["x"] | x_;
    y_ = data["y"] | y_;
    width_ = data["width"] | width_;
    rows_ = list::clampRows(data["rows"] | rows_);
    fontSize_ = data["fontsize"] | fontSize_;
    fontFace_ = fontref::parse(data["font"], TAG, "font");
    font_.store(fontref::pack(fontFace_, fontSize_), std::memory_order_release);
    page_ = data["page"] | page_;
    color_ = configColor(data, "color");
    focusColor_ = configColor(data, "focuscolor");
    focusTextColor_ = configColor(data, "focustextcolor");

    std::string options;
    int count = 0;
    for (const auto item : data["items"].as<ArduinoJson::JsonArrayConst>()) {
        const char* text = item.as<const char*>();
        if (!list::appendItem(options, count, text ? text : "")) {
            ESP_LOGW(TAG, "List: more than %d items, the rest is left out", list::kMaxItems);
            break;
        }
    }
    options.shrink_to_fit();
    options_ = std::move(options);
    count_.store(count, std::memory_order_release);
    selected_.store(list::clampSelected(data["selected"] | 0, count), std::memory_order_release);
    configSelected_ = static_cast<int16_t>(selected());
}

void List::attach() {
    auto& display = FreeButton::getLvglDisplay();
    display.lock();
    if (!linked_) {
        linkPending();
        linked_ = true;
    }
    display.unlock();
    requestRender();  // shows the roller if its page is shown
}

void List::requestRender() {
    ui::requestPendingRender();
}

int List::move(const int step) {
    int now = selected_.load(std::memory_order_acquire);
    int next;
    do {
        next = list::clampSelected(now + step, count());
        if (next == now) {
            return now;
        }
    } while (!selected_.compare_exchange_weak(now, next, std::memory_order_acq_rel));
    selectionDirty_.store(true, std::memory_order_release);
    requestRender();
    return next;
}

bool List::select(const int index) {
    if (index < 0 || index >= count()) {
        return false;
    }
    if (selected_.exchange(index, std::memory_order_acq_rel) != index) {
        selectionDirty_.store(true, std::memory_order_release);
        requestRender();
    }
    return true;
}

void List::setItems(std::string options, const int count) {
    options.shrink_to_fit();
    {
        const ItemsLock lock;
        if (!itemsReplaced_) {
            // Keep the config items for restoreConfigState(); `options`
            // then takes the empty string below.
            configOptions_.swap(options_);
            configCount_ = static_cast<int16_t>(this->count());
            itemsReplaced_ = true;
        }
        options_.swap(options);
        count_.store(count, std::memory_order_release);
        selected_.store(list::clampSelected(selected(), count), std::memory_order_release);
    }
    // `options` now holds the old string; freed here, outside the lock.
    itemsDirty_.store(true, std::memory_order_release);
    requestRender();
}

void List::setFont(const int face, const int size) {
    font_.store(fontref::pack(face, size), std::memory_order_release);
    fontDirty_.store(true, std::memory_order_release);
    requestRender();
}

void List::restoreConfigState() {
    std::string scriptOptions;  // freed outside the lock
    {
        const ItemsLock lock;
        if (itemsReplaced_) {
            scriptOptions.swap(options_);
            options_.swap(configOptions_);
            count_.store(configCount_, std::memory_order_release);
            itemsReplaced_ = false;
            itemsDirty_.store(true, std::memory_order_release);
        }
        const int configured = list::clampSelected(configSelected_, count());
        if (selected_.exchange(configured, std::memory_order_acq_rel) != configured) {
            selectionDirty_.store(true, std::memory_order_release);
        }
    }
    if (font_.load(std::memory_order_acquire) != fontref::pack(fontFace_, fontSize_)) {
        restoreFont();
    }
    requestRender();
}

std::string List::optionsCopy() const {
    const ItemsLock lock;
    return options_;
}

// LVGL lock held. Called on every render request of any widget: cheap when
// nothing changed (a page check and two atomic loads).
bool List::applyPending() {
    const bool visible = ui::visibleOnPage(page_, ui::shownPage());
    if (!visible) {
        deleteRoller();
        return false;
    }
    const bool created = roller_ == nullptr;
    if (created) {
        createRoller();
        if (roller_ == nullptr) {
            return false;
        }
    }
    if (!created && fontDirty_.exchange(false, std::memory_order_acq_rel)) {
        // The row height follows the font: the roller lays out again with
        // the visible row count it has.
        applyFont(roller_, font_.load(std::memory_order_acquire));
        lv_roller_set_visible_row_count(roller_, static_cast<uint32_t>(rows_));
    }
    if (created || itemsDirty_.exchange(false, std::memory_order_acq_rel)) {
        {
            const ItemsLock lock;  // LVGL copies the string
            lv_roller_set_options(roller_, options_.c_str(), LV_ROLLER_MODE_NORMAL);
        }
        const int sel = selected();
        if (sel >= 0) {
            lv_roller_set_selected(roller_, static_cast<uint32_t>(sel), LV_ANIM_OFF);
        }
        selectionDirty_.store(false, std::memory_order_release);
        return false;
    }
    if (selectionDirty_.exchange(false, std::memory_order_acq_rel)) {
        const int sel = selected();
        if (sel >= 0) {
            lv_roller_set_selected(roller_, static_cast<uint32_t>(sel), LV_ANIM_ON);
        }
    }
    return false;
}

// LVGL lock held.
void List::createRoller() {
    lv_obj_t* parent = FreeButton::getLvglDisplay().getMainScreen();
    if (parent == nullptr) {
        return;
    }
    if (!ui::lvglHasRoom(ui::kRoomForList)) {
        if (!buildFailed_) {
            ESP_LOGE(TAG, "List: no LVGL memory to show it");
            buildFailed_ = true;
        }
        return;  // the next render request retries
    }
    buildFailed_ = false;
    lv_obj_t* r = lv_roller_create(parent);
    if (r == nullptr) {
        ESP_LOGE(TAG, "List: no LVGL memory for the roller");
        return;
    }
    // Only our styles: no theme background, border, shadow or padding. The
    // text styles: list and list.selected (ui/ui_style.h); the item's
    // colours are local styles on top.
    lv_obj_remove_style_all(r);
    lv_obj_add_style(r, ui_style_get(STYLE_EL_LIST), LV_PART_MAIN);
    lv_obj_add_style(r, ui_style_get(STYLE_EL_LIST_SELECTED), LV_PART_SELECTED);
    fontDirty_.store(false, std::memory_order_release);
    applyFont(r, font_.load(std::memory_order_acquire));
    if (color_ != kNoColor) {
        lv_obj_set_style_text_color(r, lv_color_hex(color_), LV_PART_MAIN);
    }
    lv_obj_set_style_pad_hor(r, kTextPadding, LV_PART_MAIN);
    lv_obj_set_style_anim_duration(r, kAnimMs, LV_PART_MAIN);
    if (focusColor_ != kNoColor) {
        lv_obj_set_style_bg_color(r, lv_color_hex(focusColor_), LV_PART_SELECTED);
    }
    if (focusTextColor_ != kNoColor) {
        lv_obj_set_style_text_color(r, lv_color_hex(focusTextColor_), LV_PART_SELECTED);
    }
    lv_obj_set_style_radius(r, kFocusRadius, LV_PART_SELECTED);
    lv_obj_set_pos(r, x_ * kScreenWidth / 100, y_ * kScreenHeight / 100);
    lv_obj_set_width(r, width_ * kScreenWidth / 100);
    lv_roller_set_visible_row_count(r, static_cast<uint32_t>(rows_));
    lv_obj_remove_flag(r, LV_OBJ_FLAG_CLICKABLE);
    roller_ = r;
}

// LVGL lock held.
void List::deleteRoller() {
    if (roller_ != nullptr) {
        lv_obj_delete(roller_);
        roller_ = nullptr;
    }
}

} // namespace freebutton
