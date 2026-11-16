//
// ui_shim.cpp - see ui_shim.h. The firmware's UI calls, without LVGL.
//
// What the firmware's own code decides here: the markup grammar
// (src/ui/text_spans.c), the style cascade (src/ui/style_logic.c), the font
// table and its fallbacks (src/ui/fonts/font_logic.c), the font and padding
// of a box and of a span (src/ui/box_logic.h), the icon raster keys
// (src/ui/media_image_check.c).
//
// What this file mirrors from the LVGL-bound sources, and so has to follow
// them until LVGL itself is compiled in (stage 4): which run a widget draws
// (ui_text.c ui_render_text, ui_button.c set_markup), which base font each
// part of a box has (ui_box.c), the glue of fonts.c around font_logic.c (the
// face table, the instance table and its limit), ui_style.c's "defaults
// before a config". Not here at all: pixel layout (wrapping, clipping, flex),
// glyph metrics, the pool-low fallbacks of ui_text.c (there is always room).
//
#include "ui_shim.h"

#include <cstdio>
#include <cstring>
#include <memory>

#include "drivers/DisplayManager.h"
#include "drivers/LvglDisplay.h"
#include "fbh.h"

extern "C" {
#include "ui/box_logic.h"
#include "ui/fonts/fonts.h"
#include "ui/media_image_check.h"
#include "ui/text_spans.h"
#include "ui/ui_button.h"
#include "ui/ui_clock.h"
#include "ui/ui_style.h"
#include "ui/ui_text.h"
}
#include "ui/MediaImage.h"

struct lv_display_t {
    int index = -1;
    lv_obj_t screen;
};

namespace {

// --- fonts: fonts.c without tiny_ttf ---------------------------------------------

constexpr size_t kStringPool = 512;  // fonts.c STRING_POOL

struct Face {
    std::string name;
    std::string src;
    int32_t id = 0;
    uint16_t lineheight_x100 = 0;
    int8_t fallback = FONT_FACE_NONE;
};

Face g_faces[FONTS_MAX_FACES];
size_t g_faceCount = 0;
lv_font_t g_instances[FONTS_MAX_INSTANCES];
size_t g_instanceCount = 0;
// LV_FONT_DEFAULT: LVGL's built-in Montserrat 12, the end of every chain.
lv_font_t g_builtinFont = {FONT_FACE_NONE, 12, 15};

void setFaces(const font_face_spec_t* spec, const size_t count) {
    for (size_t f = 0; f < count; f++) {
        g_faces[f].name = spec[f].name;
        g_faces[f].src = spec[f].src ? spec[f].src : "";
        g_faces[f].id = spec[f].id;
        g_faces[f].lineheight_x100 = spec[f].lineheight_x100;
        g_faces[f].fallback = spec[f].fallback;
    }
    g_faceCount = count;
}

void ensureFaces() {
    if (g_faceCount == 0) {
        fonts_configure(nullptr, 0);
    }
}

void fontWarn(void*, const font_warn_t code, const char* name) {
    ESP_LOGW("fonts", "fonts: \"%s\": %s", name, font_logic_warn_str(code));
}

// --- styles: ui_style.c without lv_style_t ---------------------------------------------

lv_style_t g_styles[STYLE_EL_COUNT];
style_props_t g_resolved[STYLE_EL_COUNT];
bool g_resolvedSet = false;

void ensureResolved() {
    if (!g_resolvedSet) {
        style_logic_resolve(nullptr, g_resolved);
        g_resolvedSet = true;
    }
}

uint32_t styleColor(const style_element_t el) {
    return ui_style_resolved(el)->color;
}

// --- text ---------------------------------------------------------------------------

// ui_render_text(): the spans of `text` as runs. base_face / base_px / title
// as there.
void renderText(emu::Text& out, const char* text, const style_element_t el, const int baseFace, const int basePx,
                const bool title) {
    out.raw = text ? text : "";
    out.runs.clear();
    out.align = 0;
    out.valign = 0;
    out.rich = false;
    const size_t len = out.raw.size();
    std::string buf(len + 1, '\0');
    text_span_t spans[UI_TEXT_MAX_SPANS];
    const text_parse_result_t parsed =
        text_parse(out.raw.c_str(), len, buf.data(), spans, UI_TEXT_MAX_SPANS, ui_text_resolve_font, nullptr);
    out.plain.assign(buf.data(), parsed.text_len);
    out.spansCut = parsed.truncated;
    out.font = fonts_get(baseFace, basePx);
    const uint32_t baseColor = styleColor(el);
    for (size_t i = 0; i < parsed.span_count; i++) {
        const text_span_t& s = spans[i];
        if (s.align != TEXT_ALIGN_KEEP) out.align = s.align;
        if (s.valign != TEXT_VALIGN_KEEP) out.valign = s.valign;
        const lv_font_t* font = ui_text_span_font(&s, baseFace, basePx, title);
        emu::Run run;
        run.text.assign(buf.data() + s.offset, s.len);
        run.face = font->face;
        run.px = font->px;
        run.color = s.has_color ? s.color : baseColor;
        out.runs.push_back(std::move(run));
    }
}

lv_obj_t* newObject(const emu::Kind kind, const lv_obj_t* parent) {
    auto* obj = new (std::nothrow) lv_obj_t();
    if (obj) {
        obj->kind = kind;
        obj->display = parent ? parent->display : -1;
    }
    return obj;
}

// --- screens --------------------------------------------------------------------------

lv_obj_t g_mainScreen;
std::vector<std::unique_ptr<lv_display_t>> g_smallDisplays;
lv_display_t g_mainDisplay;
lv_display_t* g_defaultDisplay = &g_mainDisplay;

// --- media images: ui/MediaImage.cpp without the media store ---------------------------

constexpr int kImageSlots = 24;
lv_image_dsc_t g_images[kImageSlots];
uint32_t g_imageGeneration = 1;

emu::Icon iconOf(const lv_image_dsc_t* dsc, const uint32_t tint) {
    emu::Icon icon;
    if (dsc) {
        icon.shown = true;
        icon.key = dsc->key;
        icon.tint = tint;
    }
    return icon;
}

} // namespace

// ============================================================================
// fonts.h
// ============================================================================
extern "C" {

size_t fonts_configure(const font_def_t* defs, size_t count) {
    if (!defs) count = 0;
    font_face_spec_t spec[FONTS_MAX_FACES];
    size_t n = font_logic_builtins(spec);
    const size_t accepted =
        font_logic_merge(spec, &n, FONTS_MAX_FACES, defs, count, kStringPool, nullptr, fontWarn, nullptr);
    font_logic_resolve(spec, n, fontWarn, nullptr);
    setFaces(spec, n);
    // Instances of an earlier table name other fonts now.
    g_instanceCount = 0;
    return accepted;
}

int fonts_find(const char* name, const size_t len) {
    ensureFaces();
    if (!name || len == 0) return FONT_FACE_NONE;
    for (size_t f = 0; f < g_faceCount; f++) {
        if (g_faces[f].name.size() == len && std::memcmp(g_faces[f].name.data(), name, len) == 0) return static_cast<int>(f);
    }
    return FONT_FACE_NONE;
}

int fonts_find_id(const int32_t id) {
    ensureFaces();
    if (id < 1) return FONT_FACE_NONE;
    for (size_t f = 0; f < g_faceCount; f++) {
        if (g_faces[f].id == id) return static_cast<int>(f);
    }
    return FONT_FACE_NONE;
}

const char* fonts_face_name(const int face) {
    ensureFaces();
    return face >= 0 && static_cast<size_t>(face) < g_faceCount ? g_faces[face].name.c_str() : "";
}

int fonts_px(const int fontsize, const int fallback_px) { return font_logic_px(fontsize, fallback_px); }

// An instance per (face, px), at most FONTS_MAX_INSTANCES; beyond that, and
// for an invalid face, the built-in font, as on the device.
lv_font_t* fonts_get(const int face, int px) {
    ensureFaces();
    if (face < 0 || static_cast<size_t>(face) >= g_faceCount) return &g_builtinFont;
    if (px < FONT_PX_MIN) px = FONT_PX_MIN;
    if (px > FONT_PX_MAX) px = FONT_PX_MAX;
    for (size_t i = 0; i < g_instanceCount; i++) {
        if (g_instances[i].face == face && g_instances[i].px == px) return &g_instances[i];
    }
    if (g_instanceCount >= FONTS_MAX_INSTANCES) {
        ESP_LOGW("fonts", "fonts: no room for %s at %d px (%d instances): the built-in font is used",
                 g_faces[face].name.c_str(), px, FONTS_MAX_INSTANCES);
        return &g_builtinFont;
    }
    lv_font_t* font = &g_instances[g_instanceCount++];
    font->face = static_cast<int16_t>(face);
    font->px = static_cast<int16_t>(px);
    // The line box: the configured "lineheight", else about what the system
    // font's metrics give (the real value comes from the TTF: stage 4).
    const uint16_t lh = g_faces[face].lineheight_x100;
    font->line_height = lh ? px * lh / 100 : (px * 122 + 50) / 100;
    return font;
}

// ============================================================================
// ui_style.h
// ============================================================================

void ui_style_apply(const style_props_t resolved[STYLE_EL_COUNT]) {
    if (resolved != nullptr) {
        for (int i = 0; i < STYLE_EL_COUNT; i++) g_resolved[i] = resolved[i];
        g_resolvedSet = true;
    } else {
        ensureResolved();
    }
}

lv_style_t* ui_style_get(const style_element_t el) {
    ensureResolved();
    const int index = el < STYLE_EL_COUNT ? el : STYLE_EL_KEY_LABEL;
    g_styles[index].element = index;
    return &g_styles[index];
}

const style_props_t* ui_style_resolved(const style_element_t el) {
    ensureResolved();
    return &g_resolved[el < STYLE_EL_COUNT ? el : STYLE_EL_KEY_LABEL];
}

// ============================================================================
// ui_text.h (the parts the components and this file use)
// ============================================================================

int ui_text_resolve_font(const char* name, const size_t len, const bool is_id, const int32_t id, void* ctx) {
    (void)ctx;
    const int face = is_id ? fonts_find_id(id) : fonts_find(name, len);
    return face >= 0 ? face : TEXT_FONT_UNKNOWN;
}

lv_font_t* ui_text_span_font(const text_span_t* s, const int base_face, const int base_px, const bool title) {
    static bool unknownWarned = false;
    if (s->font == TEXT_FONT_UNKNOWN && !unknownWarned) {
        ESP_LOGW("ui_text", "text: \\f{} names an unknown font; the item's font is used (warned once)");
        unknownWarned = true;
    }
    int face;
    int px;
    box_logic_span_font(s, base_face, base_px, title, &face, &px);
    return fonts_get(face, px);
}

// ============================================================================
// ui.h: the display item box (ui_box.c)
// ============================================================================

void ui_box_create_styles(void) {}

lv_obj_t* ui_box_create(lv_obj_t* parent) {
    return newObject(emu::Kind::Box, parent);
}

void ui_box_delete(lv_obj_t* box) {
    delete box;
}

bool ui_box_set_caption(lv_obj_t* box, const char* caption) {
    const style_props_t* r = ui_style_resolved(STYLE_EL_DISPLAY_CAPTION);
    renderText(box->caption, caption, STYLE_EL_DISPLAY_CAPTION, box_logic_base_face(box->fonts.caption_face, r),
               box_logic_base_px(box->fonts.caption_fontsize, true, r), true);
    box->caption.hidden = caption == nullptr || caption[0] == '\0';
    return true;
}

bool ui_box_set_text(lv_obj_t* box, const char* text) {
    const style_props_t* r = ui_style_resolved(STYLE_EL_DISPLAY_VALUE);
    renderText(box->value, text, STYLE_EL_DISPLAY_VALUE, box_logic_base_face(box->fonts.face, r),
               box_logic_base_px(box->fonts.fontsize, false, r), false);
    return true;
}

bool ui_box_set_hint(lv_obj_t* box, const char* hint) {
    const style_props_t* v = ui_style_resolved(STYLE_EL_DISPLAY_VALUE);
    const style_props_t* u = ui_style_resolved(STYLE_EL_DISPLAY_UNIT);
    renderText(box->unit, hint, STYLE_EL_DISPLAY_UNIT, box_logic_unit_face(box->fonts.face, v, u),
               box_logic_unit_px(box->fonts.fontsize, v, u), false);
    box->unit.hidden = hint == nullptr || hint[0] == '\0';
    return true;
}

void ui_box_set_fonts(lv_obj_t* box, const ui_box_fonts_t* fonts, const int boxtype) {
    if (box == nullptr || fonts == nullptr) {
        return;
    }
    box->fonts = *fonts;
    const int step = box_logic_padding_step(fonts->fontsize, ui_style_resolved(STYLE_EL_DISPLAY_VALUE)->px);
    box->hasPadding = box_logic_padding(step, boxtype, &box->captionPad, &box->boxPad);
}

bool ui_box_update(lv_obj_t* box, const char* caption, const char* text, const char* unit, const int x, const int y,
                   const ui_box_fonts_t* fonts, const int width, const int align, const int boxtype) {
    if (boxtype == 0) box->underline = true;
    if (boxtype == 1) box->underline = false;
    ui_box_set_fonts(box, fonts, boxtype);
    ui_box_set_caption(box, caption);
    ui_box_set_text(box, text);
    ui_box_set_hint(box, unit);
    box->align = align == 0 ? LV_ALIGN_TOP_LEFT : align;
    box->x = x * 320 / 100;
    box->y = y * 240 / 100;
    box->width = width * 320 / 100;
    return true;
}

void ui_image_set_raster(lv_obj_t* img, const lv_image_dsc_t* dsc, const lv_color_t tint) {
    img->icon = iconOf(dsc, lv_color_to_u32(tint));
}

bool ui_box_set_icon(lv_obj_t* box, const lv_image_dsc_t* dsc, const lv_color_t tint) {
    if (box == nullptr) {
        return false;
    }
    box->icon = iconOf(dsc, lv_color_to_u32(tint));
    return true;
}

bool ui_box_has_icon(lv_obj_t* box) {
    return box != nullptr && box->icon.shown;
}

lv_color_t ui_box_text_color(lv_obj_t* box) {
    (void)box;
    return lv_color_hex(styleColor(STYLE_EL_DISPLAY_VALUE));
}

// ============================================================================
// ui_button.h: a key display (ui_button.c)
// ============================================================================

void ui_button_styles(void) {}

lv_obj_t* ui_button_create(lv_obj_t* parent) {
    return newObject(emu::Kind::Button, parent);
}

void ui_button_delete(lv_obj_t* button) {
    delete button;
}

// ui_button.c set_markup(): one format = the label draws the text (it wraps
// and centres) with the span's colour, font and alignment; several formats =
// a row of labels.
static void setMarkup(lv_obj_t* b, const bool title, const char* raw) {
    emu::Text& out = title ? b->title : b->text;
    const style_element_t el = title ? STYLE_EL_KEY_TOPLABEL : STYLE_EL_KEY_LABEL;
    const int face = title ? b->titleFace : b->textFace;
    const int px = title ? b->titlePx : b->textPx;
    const bool hidden = !title && b->icon.shown;

    char in[UI_BUTTON_TEXT_MAX + 1];
    char plain[UI_BUTTON_TEXT_MAX + 1];
    size_t len = raw != nullptr ? std::strlen(raw) : 0;
    if (len > UI_BUTTON_TEXT_MAX) {
        len = UI_BUTTON_TEXT_MAX;
    }
    std::memcpy(in, raw != nullptr ? raw : "", len);
    in[len] = '\0';

    text_span_t spans[UI_TEXT_MAX_SPANS];
    const text_parse_result_t parsed =
        text_parse(in, len, plain, spans, UI_TEXT_MAX_SPANS, ui_text_resolve_font, nullptr);
    if (parsed.span_count > 1) {
        renderText(out, in, el, face, px, title);
        out.rich = true;
        out.hidden = hidden;
        return;
    }
    out.raw = in;
    out.plain.assign(plain, parsed.text_len);
    out.runs.clear();
    out.rich = false;
    out.spansCut = false;
    out.hidden = hidden;
    out.align = 0;
    out.valign = 0;
    const text_span_t* s = parsed.span_count > 0 ? &spans[0] : nullptr;
    const lv_font_t* font;
    if (s != nullptr && (s->font >= 0 || s->size_kind != TEXT_SIZE_BASE)) {
        font = ui_text_span_font(s, face, px, title);
    } else {
        font = title ? b->titleFont : b->textFont;
        if (font == nullptr) {
            // The shared style's font (ui_style.h).
            const style_props_t* r = ui_style_resolved(el);
            font = fonts_get(r->face, r->px);
        }
    }
    out.font = font;
    if (s != nullptr && s->align != TEXT_ALIGN_KEEP) out.align = s->align;
    if (parsed.text_len > 0) {
        emu::Run run;
        run.text = out.plain;
        run.face = font->face;
        run.px = font->px;
        run.color = (s != nullptr && s->has_color) ? s->color : styleColor(el);
        out.runs.push_back(std::move(run));
    }
}

void ui_button_set_title(lv_obj_t* button, const char* title) {
    setMarkup(button, true, title);
}

void ui_button_set_text(lv_obj_t* button, const char* text) {
    setMarkup(button, false, text);
}

void ui_button_set_base_fonts(lv_obj_t* button, const int text_face, const int text_px, const int title_face,
                              const int title_px) {
    button->textFace = text_face;
    button->textPx = text_px;
    button->titleFace = title_face;
    button->titlePx = title_px;
}

void ui_button_set_fonts(lv_obj_t* button, const lv_font_t* text_font, const lv_font_t* title_font) {
    button->textFont = text_font;
    button->titleFont = title_font;
    // The labels take the new font at once (a single-format text keeps its
    // own \f / \s font, as a local style set after this one would).
    const std::string text = button->text.raw;
    const std::string title = button->title.raw;
    setMarkup(button, false, text.c_str());
    setMarkup(button, true, title.c_str());
}

void ui_button_set_icon(lv_obj_t* button, const lv_image_dsc_t* dsc) {
    button->icon = iconOf(dsc, styleColor(STYLE_EL_KEY_LABEL));
    button->text.hidden = dsc != nullptr;
}

// ============================================================================
// ui_clock.h: the analog clock. Its hands follow localtime() (tz.c); the
// host draws them from its clock and the zone's offset.
// ============================================================================

static struct {
    bool built = false;
    int x = 0, y = 0, width = 0, height = 0, align = 0;
} g_clock;

bool clock_init(lv_obj_t* parent, const lv_coord_t x, const lv_coord_t y, const lv_coord_t width,
                const lv_coord_t height, const lv_align_t align) {
    (void)parent;
    if (g_clock.built) {
        return false;
    }
    g_clock = {true, x, y, width, height, align};
    return true;
}

void clock_destroy(void) {
    g_clock.built = false;
}

// ============================================================================
// lvgl.h
// ============================================================================

void lv_mem_monitor(lv_mem_monitor_t* mon) {
    // As a device after boot with nothing drawn: src/lv_conf.h LV_MEM_SIZE.
    mon->total_size = 61440;
    mon->free_size = 48000;
    mon->free_biggest_size = 40000;
}

lv_display_t* lv_display_get_default(void) { return g_defaultDisplay; }
void lv_display_set_default(lv_display_t* display) { g_defaultDisplay = display ? display : &g_mainDisplay; }
lv_obj_t* lv_display_get_screen_active(lv_display_t* display) {
    return display && display != &g_mainDisplay ? &display->screen : &g_mainScreen;
}
lv_obj_t* lv_scr_act(void) { return lv_display_get_screen_active(g_defaultDisplay); }

void lv_obj_delete(lv_obj_t* obj) {
    if (obj && obj->kind != emu::Kind::Screen) delete obj;
}
void lv_obj_invalidate(lv_obj_t* obj) { (void)obj; }
void lv_obj_set_pos(lv_obj_t* obj, const int32_t x, const int32_t y) {
    obj->x = x;
    obj->y = y;
}
void lv_obj_set_width(lv_obj_t* obj, const int32_t width) { obj->width = width; }
void lv_obj_set_hidden(lv_obj_t* obj, const bool hidden) { obj->hidden = hidden; }
void lv_obj_set_clickable(lv_obj_t* obj, bool clickable) { (void)obj; (void)clickable; }
void lv_obj_remove_flag(lv_obj_t* obj, uint32_t flag) { (void)obj; (void)flag; }
void lv_obj_remove_style_all(lv_obj_t* obj) { (void)obj; }
void lv_obj_add_style(lv_obj_t* obj, const lv_style_t* style, lv_part_t part) { (void)obj; (void)style; (void)part; }

bool lv_obj_remove_local_style_prop(lv_obj_t* obj, const lv_style_prop_t prop, const lv_part_t part) {
    if (part != LV_PART_MAIN) return false;
    switch (prop) {
        case LV_STYLE_TEXT_FONT: {
            const bool had = obj->localFont != nullptr;
            obj->localFont = nullptr;
            return had;
        }
        case LV_STYLE_TEXT_LINE_SPACE: {
            const bool had = obj->hasLineSpace;
            obj->hasLineSpace = false;
            return had;
        }
        case LV_STYLE_TEXT_COLOR: {
            const bool had = obj->hasTextColor;
            obj->hasTextColor = false;
            return had;
        }
        default:
            return false;
    }
}

void lv_obj_set_style_text_font(lv_obj_t* obj, const lv_font_t* font, const lv_part_t part) {
    if (part == LV_PART_MAIN) obj->localFont = font;
}

const lv_font_t* lv_obj_get_style_text_font(lv_obj_t* obj, lv_part_t part) {
    (void)part;
    return emu::rollerFont(obj);
}

void lv_obj_set_style_text_line_space(lv_obj_t* obj, const int32_t space, const lv_part_t part) {
    if (part == LV_PART_MAIN) {
        obj->hasLineSpace = true;
        obj->lineSpace = space;
    }
}

void lv_obj_set_style_text_color(lv_obj_t* obj, const lv_color_t color, const lv_part_t part) {
    if (part == LV_PART_SELECTED) {
        obj->hasSelectedText = true;
        obj->selectedText = lv_color_to_u32(color);
    } else {
        obj->hasTextColor = true;
        obj->textColor = lv_color_to_u32(color);
    }
}

void lv_obj_set_style_bg_color(lv_obj_t* obj, const lv_color_t color, const lv_part_t part) {
    if (part == LV_PART_SELECTED) {
        obj->hasSelectedBg = true;
        obj->selectedBg = lv_color_to_u32(color);
    }
}

void lv_obj_set_style_radius(lv_obj_t* obj, int32_t radius, lv_part_t part) { (void)obj; (void)radius; (void)part; }
void lv_obj_set_style_pad_hor(lv_obj_t* obj, int32_t pad, lv_part_t part) { (void)obj; (void)pad; (void)part; }
void lv_obj_set_style_anim_duration(lv_obj_t* obj, uint32_t ms, lv_part_t part) { (void)obj; (void)ms; (void)part; }

lv_obj_t* lv_roller_create(lv_obj_t* parent) { return newObject(emu::Kind::Roller, parent); }

void lv_roller_set_options(lv_obj_t* roller, const char* options, lv_roller_mode_t mode) {
    (void)mode;
    roller->options = options ? options : "";
}

void lv_roller_set_selected(lv_obj_t* roller, const uint32_t selected, const lv_anim_enable_t anim) {
    roller->selected = static_cast<int>(selected);
    roller->animated = anim == LV_ANIM_ON;
}

void lv_roller_set_visible_row_count(lv_obj_t* roller, const uint32_t rows) {
    roller->rows = static_cast<int>(rows);
}

lv_obj_t* lv_image_create(lv_obj_t* parent) { return newObject(emu::Kind::Image, parent); }

} // extern "C"

// ============================================================================
// ui/MediaImage.h
// ============================================================================
namespace freebutton::ui {

bool iconRasterKey(char* out, const size_t cap, const char* name, const uint16_t w, const uint16_t h) {
    return media_image_icon_key(out, cap, name, w, h);
}

// One descriptor per key, counted, as the firmware's slots. nullptr when the
// bucket has no such key: the item then shows nothing.
const lv_image_dsc_t* mediaImageAcquire(const char* key) {
    if (!key || !*key) return nullptr;
    const size_t len = std::strlen(key);
    if (len >= sizeof(g_images[0].key) || !fbh_media_exists(key, len)) {
        return nullptr;
    }
    lv_image_dsc_t* freeSlot = nullptr;
    for (auto& slot : g_images) {
        if (slot.refs > 0 && std::strcmp(slot.key, key) == 0) {
            ++slot.refs;
            return &slot;
        }
        if (!freeSlot && slot.refs == 0) freeSlot = &slot;
    }
    if (!freeSlot) {
        ESP_LOGW("MediaImage", "images/%s: too many images shown at once", key);
        return nullptr;
    }
    std::strcpy(freeSlot->key, key);
    freeSlot->refs = 1;
    return freeSlot;
}

void mediaImageRelease(const lv_image_dsc_t* dsc) {
    for (auto& slot : g_images) {
        if (&slot == dsc && slot.refs > 0) {
            --slot.refs;
            return;
        }
    }
}

uint32_t mediaImageGeneration() { return g_imageGeneration; }

void mediaImageNotifyChanged() {
    ++g_imageGeneration;
}

} // namespace freebutton::ui

// ============================================================================
// drivers/LvglDisplay.h, DisplayManager.h
// ============================================================================
namespace FreeButton {

LvglDisplay& getLvglDisplay() {
    static LvglDisplay display;
    return display;
}

DisplayManager& getDisplayManager() {
    static DisplayManager manager;
    return manager;
}

lv_obj_t* LvglDisplay::getMainScreen() const { return &g_mainScreen; }

lv_display_t* LvglDisplay::getSmallDisplay(const int index) const {
    return index >= 0 && static_cast<size_t>(index) < g_smallDisplays.size() ? g_smallDisplays[index].get() : nullptr;
}

void LvglDisplay::getLvglMemory(size_t& free, size_t& total) const {
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    free = mon.free_size;
    total = mon.total_size;
}

int DisplayManager::getSmallDisplayIndexByPosition(const int buttonPosition) const {
    if (buttonPosition < 1) {
        return -1;
    }
    const int connector = (buttonPosition - 1) / 2;
    const int side = (buttonPosition - 1) % 2;
    for (size_t i = 0; i < smallDisplays_.size(); ++i) {
        if (smallDisplays_[i].connector == connector && smallDisplays_[i].position == side) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// The firmware's initializeDisplays(): two key displays per bar connector.
bool initializeDisplays(const std::vector<std::shared_ptr<Adafruit_MCP23X08>>& mcps, const int tft320Bar) {
    auto& manager = getDisplayManager();
    manager.clear();
    g_smallDisplays.clear();
    for (size_t connector = 0; connector < mcps.size(); ++connector) {
        if (!mcps[connector] || static_cast<int>(connector) == tft320Bar) {
            continue;
        }
        for (int side = 0; side < 2; ++side) {
            manager.add({static_cast<int>(connector), side});
            auto display = std::make_unique<lv_display_t>();
            display->index = static_cast<int>(g_smallDisplays.size());
            display->screen.display = display->index;
            g_smallDisplays.push_back(std::move(display));
        }
    }
    return true;
}

} // namespace FreeButton

// ============================================================================
// State records
// ============================================================================
namespace emu {

lv_obj_t* mainScreen() { return &g_mainScreen; }

const lv_font_t* rollerFont(const lv_obj_t* roller) {
    if (roller->localFont) return roller->localFont;
    const style_props_t* r = ui_style_resolved(STYLE_EL_LIST);
    return fonts_get(r->face, r->px);
}

std::string hexColor(const uint32_t color) {
    char text[8];
    std::snprintf(text, sizeof(text), "#%06x", static_cast<unsigned>(color & 0xffffffu));
    return text;
}

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

static const char* alignName(const int align) {
    switch (align) {
        case TEXT_ALIGN_LEFT: return "left";
        case TEXT_ALIGN_CENTER: return "center";
        case TEXT_ALIGN_RIGHT: return "right";
        default: return nullptr;
    }
}

static const char* valignName(const int valign) {
    switch (valign) {
        case TEXT_VALIGN_TOP: return "top";
        case TEXT_VALIGN_MIDDLE: return "middle";
        case TEXT_VALIGN_BOTTOM: return "bottom";
        default: return nullptr;
    }
}

void writeFont(Json& json, const lv_font_t* font) {
    json.beginObject();
    json.key("font").value(fonts_face_name(font->face));
    json.key("face").value(static_cast<int>(font->face));
    json.key("px").value(static_cast<int>(font->px));
    json.key("lineHeight").value(static_cast<int>(font->line_height));
    json.endObject();
}

void writeText(Json& json, const Text& text) {
    json.beginObject();
    json.key("text").value(text.raw);
    json.key("plain").value(text.plain);
    json.key("hidden").value(text.hidden);
    json.key("rich").value(text.rich);
    json.key("align");
    if (const char* a = alignName(text.align)) json.value(a); else json.null();
    json.key("valign");
    if (const char* v = valignName(text.valign)) json.value(v); else json.null();
    json.key("spansCut").value(text.spansCut);
    json.key("font");
    if (text.font) writeFont(json, text.font); else json.null();
    json.key("runs").beginArray();
    for (const Run& run : text.runs) {
        json.beginObject();
        json.key("text").value(run.text);
        json.key("font").value(fonts_face_name(run.face));
        json.key("face").value(run.face);
        json.key("px").value(run.px);
        json.key("color").value(hexColor(run.color));
        json.endObject();
    }
    json.endArray();
    json.endObject();
}

void writeIcon(Json& json, const Icon& icon) {
    if (!icon.shown) {
        json.null();
        return;
    }
    json.beginObject();
    json.key("key").value(icon.key);
    // "ico-<name>-<w>x<h>.bin" (media_image_icon_key): the icon's name and
    // raster size; any other key is a file of the images bucket.
    unsigned w = 0, h = 0;
    char name[64] = "";
    const char* key = icon.key.c_str();
    const char* dash = std::strrchr(key, '-');
    if (std::strncmp(key, "ico-", 4) == 0 && dash && dash > key + 4 && std::sscanf(dash, "-%ux%u.bin", &w, &h) == 2
        && static_cast<size_t>(dash - key - 4) < sizeof(name)) {
        std::memcpy(name, key + 4, static_cast<size_t>(dash - key - 4));
        json.key("name").value(name);
        json.key("width").value(static_cast<int>(w));
        json.key("height").value(static_cast<int>(h));
    }
    json.key("tint").value(hexColor(icon.tint));
    json.endObject();
}

void writeFonts(Json& json) {
    ensureFaces();
    json.beginArray();
    for (size_t f = 0; f < g_faceCount; f++) {
        json.beginObject();
        json.key("face").value(static_cast<int>(f));
        json.key("name").value(g_faces[f].name);
        json.key("src").value(g_faces[f].src);
        json.key("id").value(static_cast<int>(g_faces[f].id));
        json.key("fallback").value(static_cast<int>(g_faces[f].fallback));
        json.key("lineheight").value(g_faces[f].lineheight_x100 / 100.0);
        json.endObject();
    }
    json.endArray();
}

void writeStyles(Json& json) {
    static const char* const names[STYLE_EL_COUNT] = {
        "key.label", "key.toplabel", "display.value", "display.unit", "display.caption", "list", "list.selected",
    };
    json.beginObject();
    for (int el = 0; el < STYLE_EL_COUNT; el++) {
        const style_props_t* r = ui_style_resolved(static_cast<style_element_t>(el));
        json.key(names[el]).beginObject();
        json.key("font").value(fonts_face_name(r->face));
        json.key("face").value(static_cast<int>(r->face));
        json.key("px").value(static_cast<int>(r->px));
        json.key("color").value(hexColor(r->color));
        json.key("opa").value(static_cast<int>(r->opa));
        json.key("align");
        switch (r->has & STYLE_HAS_ALIGN ? r->align : 0) {
            case STYLE_ALIGN_LEFT: json.value("left"); break;
            case STYLE_ALIGN_CENTER: json.value("center"); break;
            case STYLE_ALIGN_RIGHT: json.value("right"); break;
            default: json.null(); break;
        }
        json.key("letterSpace").value(static_cast<int>(r->letter_space));
        json.key("lineSpace").value(static_cast<int>(r->line_space));
        json.key("underline").value((r->decor & STYLE_DECOR_UNDERLINE) != 0);
        json.key("strikethrough").value((r->decor & STYLE_DECOR_STRIKETHROUGH) != 0);
        if (r->has & STYLE_HAS_BG_COLOR) {
            json.key("bgColor").value(hexColor(r->bg_color));
            json.key("bgOpa").value(static_cast<int>(r->bg_opa));
        }
        json.endObject();
    }
    json.endObject();
}

// The clock, for device.cpp.
bool clockBuilt(int& x, int& y, int& width, int& height, int& align) {
    x = g_clock.x;
    y = g_clock.y;
    width = g_clock.width;
    height = g_clock.height;
    align = g_clock.align;
    return g_clock.built;
}

} // namespace emu
