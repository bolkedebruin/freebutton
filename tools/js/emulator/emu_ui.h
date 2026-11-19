//
// emu_ui.h - the emulator's own functions that take LVGL types (see emu.h).
//
#ifndef FB_EMULATOR_EMU_UI_H
#define FB_EMULATOR_EMU_UI_H
#include <lvgl.h>
#ifdef __cplusplus
extern "C" {
#endif
// seam_fonts.c: the face and px of a font of the registry; false for LVGL's
// built-in font. real: drawn from its TrueType file.
bool fbe_font_info(const lv_font_t* font, int* face, int* px, bool* real);
// seam_clock.c: the analog clock's container, NULL while there is none.
lv_obj_t* fbe_clock_container(void);
// seam_box.c: the parts of a display item's box, and the font a part's text
// has where its markup names none.
enum { FBE_BOX_CAPTION = 0, FBE_BOX_VALUE = 1, FBE_BOX_UNIT = 2, FBE_BOX_ICON = 3 };
lv_obj_t* fbe_box_part(lv_obj_t* box, int part);
const lv_font_t* fbe_box_part_font(lv_obj_t* box, int part);
#ifdef __cplusplus
}
#endif
#endif
