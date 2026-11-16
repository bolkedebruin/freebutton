//
// seam_box.c - the firmware's display item box (src/ui/ui_box.c) as it is,
// plus its parts for the host's records: ui_box.c keeps the descriptor
// (which object is the caption, the value, the unit, the icon; the item's
// fonts) private.
//
#include "ui/ui_box.c"

#include "emu_ui.h"

lv_obj_t *fbe_box_part(lv_obj_t *box, const int part) {
    const box_dsc_t *b = lv_obj_get_user_data(box);
    if (b == NULL) return NULL;
    switch (part) {
        case FBE_BOX_CAPTION: return b->title;
        case FBE_BOX_VALUE: return b->text;
        case FBE_BOX_UNIT: return b->hint;
        case FBE_BOX_ICON: return b->icon;
        default: return NULL;
    }
}

/* The font a part's text has where its markup names none: the expressions of
 * ui_box_set_caption(), ui_box_set_text() and ui_box_set_hint() above. */
const lv_font_t *fbe_box_part_font(lv_obj_t *box, const int part) {
    const box_dsc_t *b = lv_obj_get_user_data(box);
    if (b == NULL) return NULL;
    const style_props_t *v = ui_style_resolved(STYLE_EL_DISPLAY_VALUE);
    if (part == FBE_BOX_CAPTION) {
        const style_props_t *r = ui_style_resolved(STYLE_EL_DISPLAY_CAPTION);
        return fonts_get(box_logic_base_face(b->fonts.caption_face, r),
                         box_logic_base_px(b->fonts.caption_fontsize, true, r));
    }
    if (part == FBE_BOX_VALUE) {
        return fonts_get(box_logic_base_face(b->fonts.face, v), box_logic_base_px(b->fonts.fontsize, false, v));
    }
    if (part == FBE_BOX_UNIT) {
        const style_props_t *u = ui_style_resolved(STYLE_EL_DISPLAY_UNIT);
        return fonts_get(box_logic_unit_face(b->fonts.face, v, u), box_logic_unit_px(b->fonts.fontsize, v, u));
    }
    return NULL;
}
