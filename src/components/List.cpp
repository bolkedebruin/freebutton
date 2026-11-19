//
// List - see List.h.
//

#include "List.h"

#include "drivers/LvglDisplay.h"
#include "ui/LvglRoom.h"
#include "ui/MainPage.h"
#include "ui/ui_style.h"

extern "C" {
#include "ui/fonts/fonts.h"
}

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

// Fonts, line space and background of a roller (LVGL lock held), after its
// styles were added: what LVGL's shared styles do not give by themselves.
// `rows` / `selected`: the style lists the roller carries for the two parts.
//  - The rows (LV_PART_MAIN): a local font where the lookup's face and px
//    are not what the shared styles hold (the face comes from one style and
//    the size from another).
//  - The selected row (LV_PART_SELECTED) has no font of its own: the face
//    and the px a "selected" block sets, each separately, else the rows'.
//    Where no shared style of that part holds that font, and it is not the
//    rows' either (LVGL takes a font the part does not have from
//    LV_PART_MAIN), it is a local font.
//  - The line space: (line-height - 1) lines of the rows' font. The
//    built-in 1.25 is the quarter line a list always had.
//  - The background of the whole roller: a "background-color" in a main
//    block of the list's own styles. ("list" itself holds its background in
//    the element style; a main block's lv_style_t never has one, so that it
//    does nothing on a button or a display item.)
void applyLook(lv_obj_t* roller, const style_list_t rows, const style_list_t selected) {
    style_props_t r;
    ui_style_lookup(STYLE_KIND_LIST, STYLE_PART_MAIN, rows, &r, nullptr);
    int sharedFace = 0;
    int sharedPx = 0;
    if (ui_style_shared_font(STYLE_KIND_LIST, STYLE_PART_MAIN, rows, &sharedFace, &sharedPx) && sharedFace == r.face &&
        sharedPx == r.px) {
        lv_obj_remove_local_style_prop(roller, LV_STYLE_TEXT_FONT, LV_PART_MAIN);
    } else {
        lv_obj_set_style_text_font(roller, fonts_get(r.face, r.px), LV_PART_MAIN);
    }

    style_props_t s;
    ui_style_lookup(STYLE_KIND_LIST, STYLE_PART_SELECTED, selected, &s, nullptr);
    int selectedFace = 0;
    int selectedPx = 0;
    style_logic_follow_font(&s, r.face, r.px, &selectedFace, &selectedPx);
    const bool held = ui_style_shared_font(STYLE_KIND_LIST, STYLE_PART_SELECTED, selected, &sharedFace, &sharedPx);
    const bool given = held ? (sharedFace == selectedFace && sharedPx == selectedPx)
                            : (selectedFace == r.face && selectedPx == r.px);
    if (given) {
        lv_obj_remove_local_style_prop(roller, LV_STYLE_TEXT_FONT, LV_PART_SELECTED);
    } else {
        lv_obj_set_style_text_font(roller, fonts_get(selectedFace, selectedPx), LV_PART_SELECTED);
    }

    const lv_font_t* font = lv_obj_get_style_text_font(roller, LV_PART_MAIN);
    lv_obj_set_style_text_line_space(roller, style_logic_line_space(font->line_height, r.line_height), LV_PART_MAIN);

    const style_props_t* kind = ui_style_resolved(STYLE_EL_LIST);
    if ((r.has & STYLE_HAS_BG) && !((kind->has & STYLE_HAS_BG) && kind->bg == r.bg)) {
        lv_obj_set_style_bg_color(roller, lv_color_hex(r.bg & 0xFFFFFFu), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(roller, static_cast<lv_opa_t>(r.bg >> 24), LV_PART_MAIN);
    } else {
        lv_obj_remove_local_style_prop(roller, LV_STYLE_BG_COLOR, LV_PART_MAIN);
        lv_obj_remove_local_style_prop(roller, LV_STYLE_BG_OPA, LV_PART_MAIN);
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
    const style_list_t list = styleref::parse(data["style"], TAG);
    styleref::store(configStyle_, list);
    style_.store(list, std::memory_order_release);
    page_ = data["page"] | page_;

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

void List::setStyles(const style_list_t list) {
    if (style_.exchange(list, std::memory_order_acq_rel) == list) {
        return;  // the list it has: nothing to draw
    }
    styleDirty_.store(true, std::memory_order_release);
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
    const style_list_t configured = configStyles();
    if (style_.exchange(configured, std::memory_order_acq_rel) != configured) {
        styleDirty_.store(true, std::memory_order_release);
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
    // Also when a script changed a value of a style the list uses
    // (fb.style.set): LVGL restyles the roller itself, but not what
    // applyLook() sets (the background of a main block).
    const bool restyled = ui_style_changed(STYLE_KIND_LIST, style_.load(std::memory_order_acquire));
    if (!created && (styleDirty_.exchange(false, std::memory_order_acq_rel) || restyled)) {
        // The row height follows the font: the roller lays out again with
        // the visible row count it has.
        applyStyles();
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

// LVGL lock held, roller exists. The list's styles go on after the element
// styles (createRoller), so they win over them: the main blocks on the rows,
// the "selected" blocks on the selected row.
void List::applyStyles() {
    const style_list_t style = style_.load(std::memory_order_acquire);
    // The rows and the selected row together: without room in the LVGL pool
    // for both, both keep what they carried (never the rows of the new look
    // with the selection bar of the old one, or the other way round).
    const ui_style_target_t targets[] = {
        {roller_, &styleCarried_, STYLE_PART_MAIN, LV_PART_MAIN},
        {roller_, &selectedCarried_, STYLE_PART_SELECTED, LV_PART_SELECTED},
    };
    const bool attached = ui_style_attach_all(targets, 2, style);
    // The look follows what the roller carries (styles that found no room
    // in the LVGL pool are not there).
    applyLook(roller_, styleCarried_, selectedCarried_);
    if (!attached) {
        // Again in a later walk (applyPending), not at once: that would
        // spin while the pool stays low.
        styleDirty_.store(true, std::memory_order_release);
        ui::requestPendingRenderLater();
    }
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
    // text styles: the list elements (ui/ui_style.h), then the list's own.
    lv_obj_remove_style_all(r);
    lv_obj_add_style(r, ui_style_get(STYLE_EL_LIST), LV_PART_MAIN);
    lv_obj_add_style(r, ui_style_get(STYLE_EL_LIST_SELECTED), LV_PART_SELECTED);
    styleDirty_.store(false, std::memory_order_release);
    roller_ = r;
    styleCarried_ = 0;
    selectedCarried_ = 0;
    applyStyles();
    lv_obj_set_style_pad_hor(r, kTextPadding, LV_PART_MAIN);
    lv_obj_set_style_anim_duration(r, kAnimMs, LV_PART_MAIN);
    lv_obj_set_style_radius(r, kFocusRadius, LV_PART_SELECTED);
    lv_obj_set_pos(r, x_ * kScreenWidth / 100, y_ * kScreenHeight / 100);
    lv_obj_set_width(r, width_ * kScreenWidth / 100);
    lv_roller_set_visible_row_count(r, static_cast<uint32_t>(rows_));
    lv_obj_remove_flag(r, LV_OBJ_FLAG_CLICKABLE);
}

// LVGL lock held.
void List::deleteRoller() {
    if (roller_ != nullptr) {
        lv_obj_delete(roller_);
        roller_ = nullptr;
    }
}

} // namespace freebutton
