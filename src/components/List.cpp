//
// List - see List.h.
//

#include "List.h"

#include "drivers/LvglDisplay.h"
#include "ui/LvglRoom.h"
#include "ui/MainPage.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

extern "C" {
extern lv_font_t* fnt_medium_14;
extern lv_font_t* fnt_medium_24;
extern lv_font_t* fnt_medium_32;
extern lv_font_t* fnt_medium_48;
extern lv_font_t* fnt_medium_72;
}

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

// Same mapping as display items (ui_text.c).
lv_font_t* fontFor(const int size) {
    switch (size) {
        case 1: return fnt_medium_14;
        case 2: return fnt_medium_24;
        case 4: return fnt_medium_48;
        case 5: return fnt_medium_72;
        default: return fnt_medium_32;
    }
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
    page_ = data["page"] | page_;
    color_ = list::parseColor(data["color"] | "", color_);
    focusColor_ = list::parseColor(data["focuscolor"] | "", focusColor_);
    focusTextColor_ = list::parseColor(data["focustextcolor"] | "", focusTextColor_);

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
        options_.swap(options);
        count_.store(count, std::memory_order_release);
        selected_.store(list::clampSelected(selected(), count), std::memory_order_release);
    }
    // `options` now holds the old string; freed here, outside the lock.
    itemsDirty_.store(true, std::memory_order_release);
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
    // Only our styles: no theme background, border, shadow or padding.
    lv_obj_remove_style_all(r);
    lv_font_t* font = fontFor(fontSize_);
    lv_obj_set_style_text_font(r, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(r, lv_color_hex(color_), LV_PART_MAIN);
    lv_obj_set_style_text_align(r, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    lv_obj_set_style_text_line_space(r, font->line_height / 4, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(r, kTextPadding, LV_PART_MAIN);
    lv_obj_set_style_anim_duration(r, kAnimMs, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_PART_SELECTED);
    lv_obj_set_style_bg_color(r, lv_color_hex(focusColor_), LV_PART_SELECTED);
    lv_obj_set_style_text_color(r, lv_color_hex(focusTextColor_), LV_PART_SELECTED);
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
