//
// seam_box.c - the firmware's display item box (src/ui/ui_box.c) as it is,
// plus its parts for the host's records: ui_box.c keeps the descriptor
// (which object is the caption, the value, the unit, the icon) private.
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

/* The font a part's text has where its markup names none: what ui_box.c
 * itself starts from (ui_box_part_font). FBE_BOX_CAPTION / VALUE / UNIT are
 * UI_BOX_CAPTION / VALUE / UNIT. */
const lv_font_t *fbe_box_part_font(lv_obj_t *box, const int part) {
    if (lv_obj_get_user_data(box) == NULL || part < UI_BOX_CAPTION || part > UI_BOX_UNIT) return NULL;
    int face;
    int px;
    ui_box_part_font(box, part, &face, &px, NULL);
    return fonts_get(face, px);
}
