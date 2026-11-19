// LVGL configuration of fb-emulator.wasm: the firmware's src/lv_conf.h as it
// is (same pool size, widgets, fonts, colour depth), with one override.
//
// LV_ASSERT_HANDLER: the firmware halts the LVGL task (while(1);) when LVGL
// asserts, e.g. on a failed pool allocation. In a page that would hang the
// tab, so the emulator traps instead: the host reports "LVGL halted" and
// discards the instance, which is what a hung display task is to the user.
#ifndef FB_EMULATOR_LV_CONF_OVERRIDE_H
#define FB_EMULATOR_LV_CONF_OVERRIDE_H

#include "../../../src/lv_conf.h"

#undef LV_ASSERT_HANDLER
#define LV_ASSERT_HANDLER __builtin_trap();

#endif
