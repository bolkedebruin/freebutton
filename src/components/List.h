//
// List - a selection list on the main display (config.json "lists"), drawn as
// an LVGL roller: `rows` rows visible, the selected item in the middle row on
// a rounded bar, a short scroll animation when the selection moves.
//
// Scripts drive it by name (fb.main.list.next("scenes"), ...); the keys that
// move it are the script's business.
//
// Memory: the items are one options string in the general heap
// ("Bed Time\nEvening", ListLogic.h). The roller (LVGL objects, styles and
// its own copy of the options) exists only while the list's page is shown:
// it is created when the page appears and deleted when it goes, so a list
// costs no LVGL memory on other pages.
//
// Threads: scripts (JS task) change the selection and the items; the LVGL
// task renders them in applyPending() (PendingRender), like display items.
// The selection is an atomic; the items string is guarded by a small static
// mutex that the LVGL task takes only while it hands the string to the
// roller (never while a script waits on the LVGL lock: scripts never take it).
//

#ifndef FREEBUTTON_LIST_H
#define FREEBUTTON_LIST_H

#include "ui/MainPage.h"
#include <atomic>
#include <memory>
#include <string>
#include <string_view>

#include "ArduinoJson.h"
#include "ListLogic.h"
#include "FontRef.h"
#include "json/JsonWriter.h"
#include "ui/PendingRender.h"

#include <lvgl.h>

namespace freebutton {

class List final : public ui::PendingRender {
public:
    List() = default;
    ~List();

    static std::unique_ptr<List> fromJson(const ArduinoJson::JsonObject& config);

    // Links the list for rendering (LVGL lock taken here). Call once, after
    // configuration; the roller itself appears when the page is shown.
    void attach();

    // --- any task (scripts) ----------------------------------------------------
    int count() const { return count_.load(std::memory_order_acquire); }
    int selected() const { return selected_.load(std::memory_order_acquire); }
    // Moves the selection by step (clamped); returns the new selection.
    int move(int step);
    // false when index is out of range (nothing changes).
    bool select(int index);
    // Replaces the items (options string and count from list::appendItem).
    void setItems(std::string options, int count);
    // Font of the items (a fonts.h face, FONT_FACE_NONE = the list style's;
    // size 1..5 = step, else px, 0 = the style's).
    void setFont(int face, int size);
    // Back to the configured "font" / "fontsize".
    void restoreFont() { setFont(fontFace_, fontSize_); }

    int getPage() const { return page_; }

    // A config colour that is not set (the list style's applies).
    static constexpr unsigned long kNoColor = 0xFFFFFFFFul;

    // Copy of the options string ("a\nb"), for the config writer (/config,
    // rare): taken under the items mutex so the writer never holds it while
    // it waits on the network.
    std::string optionsCopy() const;

    template<typename OutputStream>
    void toJson(json::JsonWriter<OutputStream>::ScopedObject& obj) const;

    // PendingRender (LVGL task, LVGL lock held).
    bool applyPending() override;
    bool shownNow() const override { return ui::visibleOnPage(page_, ui::shownPage()); }

private:
    static constexpr auto TAG = "List";

    void configure(const ArduinoJson::JsonObject& data);
    void createRoller();   // LVGL lock held
    void deleteRoller();   // LVGL lock held
    void requestRender();

    // Configuration (written before attach(), read-only afterwards).
    int x_ = 0;
    int y_ = 0;
    int width_ = 50;
    int rows_ = list::kDefaultRows;
    // Config "font" / "fontsize" and the colours: the local layer over the
    // list / list.selected styles (ui/ui_style.h); FONT_FACE_NONE, 0 and
    // kNoColor = the style's.
    int fontSize_ = 0;
    int fontFace_ = FONT_FACE_NONE;
    int page_ = 1;
    unsigned long color_ = kNoColor;
    unsigned long focusColor_ = kNoColor;
    unsigned long focusTextColor_ = kNoColor;

    // Items: options_ under the items mutex; count_ and selected_ atomic.
    std::string options_;
    std::atomic<int> count_{0};
    std::atomic<int> selected_{-1};
    std::atomic<bool> itemsDirty_{false};
    std::atomic<bool> selectionDirty_{false};
    // The font now (fontref::pack): the configured one until setFont().
    std::atomic<uint32_t> font_{fontref::pack(FONT_FACE_NONE, 0)};
    std::atomic<bool> fontDirty_{false};

    lv_obj_t* roller_ = nullptr;  // LVGL lock held
    bool linked_ = false;         // LVGL lock held
    bool buildFailed_ = false;    // LVGL lock held; logged once
};

template<typename OutputStream>
void List::toJson(json::JsonWriter<OutputStream>::ScopedObject& obj) const {
    obj.Add("page", page_);
    obj.Add("x", x_);
    obj.Add("y", y_);
    obj.Add("width", width_);
    obj.Add("rows", rows_);
    if (fontSize_ != 0) {
        obj.Add("fontsize", fontSize_);
    }
    if (fontFace_ != FONT_FACE_NONE) {
        obj.Add("font", fonts_face_name(fontFace_));
    }
    obj.Add("selected", selected());
    char hex[7];
    const auto addColor = [&](const char* key, const unsigned long c) {
        if (c != kNoColor) {
            snprintf(hex, sizeof(hex), "%06lx", c);
            obj.Add(key, static_cast<const char*>(hex));
        }
    };
    addColor("color", color_);
    addColor("focuscolor", focusColor_);
    addColor("focustextcolor", focusTextColor_);
    auto items = obj.Array("items");
    const std::string options = optionsCopy();
    if (count() > 0) {
        size_t start = 0;
        while (true) {
            const size_t end = options.find('\n', start);
            items.Add(std::string_view(options).substr(start, end == std::string::npos ? std::string::npos : end - start));
            if (end == std::string::npos) {
                break;
            }
            start = end + 1;
        }
    }
}

} // namespace freebutton

#endif // FREEBUTTON_LIST_H
