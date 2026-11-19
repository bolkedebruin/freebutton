//
// records.cpp - see records.h. Nothing here decides what is shown: it reads
// LVGL objects, the font registry (seam_fonts.c) and the resolved styles.
//
#include "records.h"

#include <cstdio>
#include <cstring>

#include "emu.h"
#include "emu_ui.h"

extern "C" {
#include "ui/fonts/fonts.h"
#include "ui/style_logic.h"
#include "ui/text_spans.h"
#include "ui/ui_style.h"
#include "ui/ui_text.h"
}

namespace emu {

std::string hexColor(const uint32_t color) {
    char text[8];
    std::snprintf(text, sizeof(text), "#%06x", static_cast<unsigned>(color & 0xffffffu));
    return text;
}

std::string hexColor(const lv_color_t color) { return hexColor(lv_color_to_u32(color)); }

void Json::comma() {
    if (!first_) out_.push_back(',');
    first_ = false;
}
Json& Json::beginObject() { comma(); out_.push_back('{'); first_ = true; return *this; }
Json& Json::endObject() { out_.push_back('}'); first_ = false; return *this; }
Json& Json::beginArray() { comma(); out_.push_back('['); first_ = true; return *this; }
Json& Json::endArray() { out_.push_back(']'); first_ = false; return *this; }
Json& Json::key(const char* name) {
    comma();
    out_.push_back('"');
    out_ += name;
    out_ += "\":";
    first_ = true;
    return *this;
}
Json& Json::value(const std::string& text) {
    comma();
    out_.push_back('"');
    for (const char c : text) {
        const auto u = static_cast<unsigned char>(c);
        if (c == '"' || c == '\\') {
            out_.push_back('\\');
            out_.push_back(c);
        } else if (u < 0x20) {
            char esc[8];
            std::snprintf(esc, sizeof(esc), "\\u%04x", u);
            out_ += esc;
        } else {
            out_.push_back(c);
        }
    }
    out_.push_back('"');
    return *this;
}
Json& Json::value(const char* text) { return value(std::string(text ? text : "")); }
Json& Json::value(const int number) { comma(); out_ += std::to_string(number); return *this; }
Json& Json::value(const unsigned long number) { comma(); out_ += std::to_string(number); return *this; }
Json& Json::value(const double number) {
    comma();
    char text[32];
    std::snprintf(text, sizeof(text), "%.6g", number);
    out_ += text;
    return *this;
}
Json& Json::value(const bool flag) { comma(); out_ += flag ? "true" : "false"; return *this; }
Json& Json::null() { comma(); out_ += "null"; return *this; }
Json& Json::raw(const std::string& json) { comma(); out_ += json; return *this; }

namespace {

struct FontId {
    int face = -1;
    int px = 0;
    bool real = false;
};

FontId fontId(const lv_font_t* font) {
    FontId id;
    if (!font || !fbe_font_info(font, &id.face, &id.px, &id.real)) {
        id.face = -1;
        id.px = font ? font->line_height : 0;
        id.real = false;
    }
    return id;
}

void writeRuns(Json& json, lv_obj_t* obj, std::string& plain) {
    if (lv_obj_check_type(obj, &lv_label_class)) {
        const char* text = lv_label_get_text(obj);
        if (text && *text) {
            const FontId id = fontId(lv_obj_get_style_text_font(obj, LV_PART_MAIN));
            json.beginObject();
            json.key("text").value(text);
            json.key("font").value(fonts_face_name(id.face));
            json.key("face").value(id.face);
            json.key("px").value(id.px);
            json.key("color").value(hexColor(lv_obj_get_style_text_color(obj, LV_PART_MAIN)));
            json.endObject();
            plain += text;
        }
        return;
    }
    const uint32_t count = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < count; ++i) {
        lv_obj_t* child = lv_obj_get_child(obj, static_cast<int32_t>(i));
        if (!lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN)) writeRuns(json, child, plain);
    }
}

const char* alignName(const int align) {
    switch (align) {
        case TEXT_ALIGN_LEFT: return "left";
        case TEXT_ALIGN_CENTER: return "center";
        case TEXT_ALIGN_RIGHT: return "right";
        default: return nullptr;
    }
}

const char* valignName(const int valign) {
    switch (valign) {
        case TEXT_VALIGN_TOP: return "top";
        case TEXT_VALIGN_MIDDLE: return "middle";
        case TEXT_VALIGN_BOTTOM: return "bottom";
        default: return nullptr;
    }
}

} // namespace

void writeFont(Json& json, const lv_font_t* font) {
    const FontId id = fontId(font);
    json.beginObject();
    json.key("font").value(fonts_face_name(id.face));
    json.key("face").value(id.face);
    json.key("px").value(id.px);
    json.key("lineHeight").value(font ? static_cast<int>(font->line_height) : 0);
    json.key("real").value(id.real);
    json.endObject();
}

void writeText(Json& json, lv_obj_t* obj, const std::string& raw, const bool rich, const lv_font_t* baseFont) {
    // The markup's own alignment (the last \al \ac \ar, \vt \vm \vb), as
    // ui_render_text() and ui_button.c apply it.
    int align = TEXT_ALIGN_KEEP;
    int valign = TEXT_VALIGN_KEEP;
    bool spansCut = false;
    {
        std::string buf(raw.size() + 1, '\0');
        text_span_t spans[UI_TEXT_MAX_SPANS];
        const text_parse_result_t parsed =
            text_parse(raw.c_str(), raw.size(), buf.data(), spans, UI_TEXT_MAX_SPANS, ui_text_resolve_font, nullptr);
        for (size_t i = 0; i < parsed.span_count; ++i) {
            if (spans[i].align != TEXT_ALIGN_KEEP) align = spans[i].align;
            if (spans[i].valign != TEXT_VALIGN_KEEP) valign = spans[i].valign;
        }
        spansCut = parsed.truncated;
    }
    json.beginObject();
    json.key("text").value(raw);
    std::string plain;
    Json runs;
    runs.beginArray();
    if (obj) writeRuns(runs, obj, plain);
    runs.endArray();
    json.key("plain").value(plain);
    json.key("hidden").value(!obj || lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN));
    json.key("rich").value(rich);
    json.key("align");
    if (const char* a = alignName(align)) json.value(a); else json.null();
    json.key("valign");
    if (const char* v = valignName(valign)) json.value(v); else json.null();
    json.key("spansCut").value(spansCut);
    json.key("font");
    if (baseFont) writeFont(json, baseFont); else json.null();
    json.key("runs");
    json.raw(runs.str());
    json.endObject();
}

void writeIcon(Json& json, lv_obj_t* image) {
    const lv_image_dsc_t* dsc = nullptr;
    if (image && !lv_obj_has_flag(image, LV_OBJ_FLAG_HIDDEN) && lv_image_get_src(image) &&
        lv_image_src_get_type(lv_image_get_src(image)) == LV_IMAGE_SRC_VARIABLE) {
        dsc = static_cast<const lv_image_dsc_t*>(lv_image_get_src(image));
    }
    const char* key = dsc ? fbe_media_key_of(dsc->data) : nullptr;
    if (!key) {
        json.null();
        return;
    }
    json.beginObject();
    json.key("key").value(key);
    // "ico-<name>-<w>x<h>.bin" (media_image_icon_key): the icon's name and
    // raster size; any other key is a file of the images bucket.
    unsigned w = 0, h = 0;
    char name[64] = "";
    const char* dash = std::strrchr(key, '-');
    if (std::strncmp(key, "ico-", 4) == 0 && dash && dash > key + 4 && std::sscanf(dash, "-%ux%u.bin", &w, &h) == 2
        && static_cast<size_t>(dash - key - 4) < sizeof(name)) {
        std::memcpy(name, key + 4, static_cast<size_t>(dash - key - 4));
        json.key("name").value(name);
        json.key("width").value(static_cast<int>(w));
        json.key("height").value(static_cast<int>(h));
    } else {
        json.key("width").value(static_cast<int>(dsc->header.w));
        json.key("height").value(static_cast<int>(dsc->header.h));
    }
    json.key("tint").value(hexColor(lv_obj_get_style_image_recolor(image, LV_PART_MAIN)));
    json.endObject();
}

void writeCoords(Json& json, lv_obj_t* obj) {
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    json.key("x").value(static_cast<int>(a.x1));
    json.key("y").value(static_cast<int>(a.y1));
    json.key("width").value(static_cast<int>(a.x2 - a.x1 + 1));
    json.key("height").value(static_cast<int>(a.y2 - a.y1 + 1));
}

void writeFonts(Json& json) {
    json.beginArray();
    const size_t count = fbe_font_face_count();
    for (size_t f = 0; f < count; f++) {
        const char* name = "";
        const char* key = "";
        int32_t id = 0;
        int fallback = -1;
        int lineheight = 0;
        bool usable = false;
        fbe_font_face(f, &name, &key, &id, &fallback, &lineheight, &usable);
        json.beginObject();
        json.key("face").value(static_cast<int>(f));
        json.key("name").value(name);
        json.key("src").value(key);
        json.key("id").value(static_cast<int>(id));
        json.key("fallback").value(fallback);
        json.key("lineheight").value(lineheight / 100.0);
        // false: the bucket has no such file or it is not a usable TrueType
        // font, and its text is drawn in LVGL's built-in font.
        json.key("loaded").value(usable);
        json.endObject();
    }
    json.endArray();
}

// The seven elements (the look of each kind's parts after "button",
// "displayitem" and "list" were resolved with the built-in look), under the
// names "<kind>" and "<kind>.<part>", then "icons" (the kinds' icon colours)
// and "names" (the other styles of the config, in file order).
void writeStyles(Json& json) {
    static const char* const names[STYLE_EL_COUNT] = {
        "button", "button.title", "displayitem", "displayitem.unit", "displayitem.title", "list", "list.selected",
    };
    json.beginObject();
    for (int el = 0; el < STYLE_EL_COUNT; el++) {
        const style_props_t* r = ui_style_resolved(static_cast<style_element_t>(el));
        json.key(names[el]).beginObject();
        json.key("font");
        if (r->has & STYLE_HAS_FONT) json.value(fonts_face_name(r->face)); else json.null();
        json.key("face").value((r->has & STYLE_HAS_FONT) ? static_cast<int>(r->face) : -1);
        json.key("px").value((r->has & STYLE_HAS_SIZE) ? static_cast<int>(r->px) : 0);
        json.key("color").value(hexColor(static_cast<uint32_t>(r->color)));
        json.key("opa").value((r->has & STYLE_HAS_OPA) ? static_cast<int>(r->opa) : 255);
        json.key("align");
        switch (style_logic_text_align(r)) {
            case STYLE_ALIGN_LEFT: json.value("left"); break;
            case STYLE_ALIGN_CENTER: json.value("center"); break;
            case STYLE_ALIGN_RIGHT: json.value("right"); break;
            default: json.null(); break;
        }
        json.key("letterSpace").value(static_cast<int>(r->letter_space));
        // x100; 0: no line-height (LVGL's own line space)
        json.key("lineHeight").value((r->has & STYLE_HAS_LINE_HEIGHT) ? static_cast<int>(r->line_height) : 0);
        json.key("underline").value((r->decor & STYLE_DECOR_UNDERLINE) != 0);
        json.key("lineThrough").value((r->decor & STYLE_DECOR_LINE_THROUGH) != 0);
        if (r->has & STYLE_HAS_BG) {
            json.key("bgColor").value(hexColor(static_cast<uint32_t>(r->bg & 0xFFFFFFu)));
            json.key("bgOpa").value(static_cast<int>(r->bg >> 24));
        }
        json.endObject();
    }
    json.endObject();
}

void writeStyleNames(Json& json) {
    json.beginArray();
    for (int ref = 1; ref <= STYLE_NAMED_MAX; ref++) {
        size_t len = 0;
        const char* name = ui_style_name(static_cast<uint8_t>(ref), &len);
        if (name == nullptr) break;
        // a style the LVGL pool had no room for is not there for items and scripts
        if (ui_style_find(name, len) != 0) json.value(std::string(name, len));
    }
    json.endArray();
}

} // namespace emu
