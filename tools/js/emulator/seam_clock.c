//
// seam_clock.c - the firmware's analog clock (src/ui/ui_clock.c) as it is,
// plus its container for the host's layout record (ui_clock.c keeps the one
// clock in a static).
//
#include "ui/ui_clock.c"

#include "emu_ui.h"

lv_obj_t *fbe_clock_container(void) { return g_clock != NULL ? g_clock->container : NULL; }
