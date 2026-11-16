//
// ui_shim.h - the virtual widgets behind the firmware's UI calls.
//
// On the device, the components (src/components) draw through ui_box.c,
// ui_button.c, ui_clock.c and LVGL's roller and image widgets. LVGL is not in
// the emulator (stage 4), so ui_shim.cpp implements those functions on the
// structs below: each keeps what the firmware asked to show, resolved with
// the firmware's own rules (text_spans.c, style_logic.c, font_logic.c,
// box_logic.h). device.cpp turns them into the host's state records.
//
#ifndef FB_EMULATOR_UI_SHIM_H
#define FB_EMULATOR_UI_SHIM_H

#include <cstdint>
#include <string>
#include <vector>

#include <lvgl.h>

extern "C" {
#include "ui/style_logic.h"
#include "ui/ui.h"
}

namespace emu {

// One run of text in one format.
struct Run {
    std::string text;
    int face = -1;  // fonts.h face; -1 = LVGL's built-in font (no face, or no room for the instance)
    int px = 0;
    uint32_t color = 0;  // 0xRRGGBB
};

// A text as drawn: the runs of ui_render_text() / ui_button.c set_markup().
struct Text {
    std::string raw;        // as given, markup included (cut to the widget's limit)
    std::string plain;      // without markup
    std::vector<Run> runs;
    const lv_font_t* font = nullptr;  // the text's own font (what a run without \f / \s gets)
    int align = 0;          // text_align_kind_t of the markup (\al \ac \ar); 0 = none
    int valign = 0;         // text_valign_kind_t; 0 = none
    bool hidden = false;    // takes no room (an empty caption / unit, a label behind an icon)
    bool rich = false;      // a key text in several formats: a row, it does not wrap
    bool spansCut = false;  // more formats than UI_TEXT_MAX_SPANS
};

struct Icon {
    bool shown = false;
    std::string key;  // images-bucket key
    uint32_t tint = 0;
};

enum class Kind : uint8_t { Screen, Box, Button, Roller, Image };

} // namespace emu

// The lv_obj_t of shim/lvgl.h.
struct lv_obj_t {
    emu::Kind kind = emu::Kind::Screen;
    int display = -1;  // -1 = the main display, else the key display's index

    // Position and size in px (lv_obj_set_pos / _set_width, ui_box_update).
    int x = 0, y = 0, width = 0;
    bool hidden = false;

    // Box (ui_box.c).
    emu::Text caption, value, unit;
    ui_box_fonts_t fonts = {-1, 0, -1, 0};
    int align = 0;        // lv_align_t of the box
    bool underline = false;
    bool hasPadding = false;
    int captionPad = 0, boxPad = 0;
    emu::Icon icon;

    // Button (ui_button.c).
    emu::Text title, text;
    const lv_font_t* titleFont = nullptr;
    const lv_font_t* textFont = nullptr;
    int titleFace = 0, titlePx = 0, textFace = 0, textPx = 0;

    // Roller (LVGL) and image.
    std::string options;
    int selected = 0;
    int rows = 0;
    bool animated = false;
    const lv_font_t* localFont = nullptr;
    bool hasLineSpace = false;
    int lineSpace = 0;
    bool hasTextColor = false, hasSelectedBg = false, hasSelectedText = false;
    uint32_t textColor = 0, selectedBg = 0, selectedText = 0;
};

namespace emu {

// "#rrggbb"
std::string hexColor(uint32_t color);

// A minimal JSON text builder (the records the host parses).
class Json {
public:
    Json& beginObject();
    Json& endObject();
    Json& beginArray();
    Json& endArray();
    Json& key(const char* name);
    Json& value(const std::string& text);
    Json& value(const char* text);
    Json& value(int number);
    Json& value(unsigned long number);
    Json& value(double number);
    Json& value(bool flag);
    Json& null();
    const std::string& str() const { return out_; }

private:
    void comma();
    std::string out_;
    bool first_ = true;
};

// Writers for the parts of a state record.
void writeText(Json& json, const Text& text);
void writeIcon(Json& json, const Icon& icon);
void writeFont(Json& json, const lv_font_t* font);
// The font table and the resolved styles (FBH_STATE_DEVICE).
void writeFonts(Json& json);
void writeStyles(Json& json);

// The font of the roller as drawn: its local font, else the list style's.
const lv_font_t* rollerFont(const lv_obj_t* roller);

// The key display's index of a screen, -1 for the main screen.
lv_obj_t* mainScreen();

} // namespace emu

#endif // FB_EMULATOR_UI_SHIM_H
