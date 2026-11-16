//
// records.h - the JSON state records the host gets (fbh_state), read from the
// LVGL objects the firmware made.
//
#ifndef FB_EMULATOR_RECORDS_H
#define FB_EMULATOR_RECORDS_H

#include <cstdint>
#include <string>

#include <lvgl.h>

namespace emu {

// "#rrggbb"
std::string hexColor(uint32_t color);
std::string hexColor(lv_color_t color);

// A minimal JSON text builder.
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
    // JSON text made elsewhere, as one value.
    Json& raw(const std::string& json);
    const std::string& str() const { return out_; }

private:
    void comma();
    std::string out_;
    bool first_ = true;
};

// {font, face, px, lineHeight, real}: a font of the firmware's registry, or
// LVGL's built-in font (face -1; also what stands in for a font file that is
// missing: real = false).
void writeFont(Json& json, const lv_font_t* font);

// A text as LVGL draws it: the labels inside `obj` (the object itself when it
// is a label), each one run with its font and colour. `raw` is the text the
// firmware was given (markup included); its alignment and whether it had
// more formats than the firmware draws come from the firmware's parser.
// `baseFont`: the font the text has where the markup names none.
void writeText(Json& json, lv_obj_t* obj, const std::string& raw, bool rich, const lv_font_t* baseFont);

// The raster an lv_image shows: {key, name?, width?, height?, tint}; null for
// no object, a hidden one, or one without a source.
void writeIcon(Json& json, lv_obj_t* image);

// "x","y","width","height": the object's place on its display, in pixels
// (lv_obj_get_coords: after layout, alignment included).
void writeCoords(Json& json, lv_obj_t* obj);

// The font table and the resolved styles (FBH_STATE_DEVICE).
void writeFonts(Json& json);
void writeStyles(Json& json);

} // namespace emu

#endif // FB_EMULATOR_RECORDS_H
