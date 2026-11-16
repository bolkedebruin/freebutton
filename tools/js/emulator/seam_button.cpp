//
// seam_button.cpp - the key bindings, for fb-emulator.wasm.
//
// src/scripting/button_hardware.cpp is the firmware's, included unchanged.
// The dispatch functions are wrapped: the host hears of every key event with
// what the script's handler decided (it returned false: the firmware
// publishes nothing for this press), and an exception a handler left pending
// (the glue ignores it) is reported.
//
#include <Arduino.h>

#include "emu.h"
#include "fbh.h"
#include "types.h"

#define button_hw_dispatch_click fbe_firmware_dispatch_click
#define button_hw_dispatch_long_press fbe_firmware_dispatch_long_press
#define button_hw_dispatch_release fbe_firmware_dispatch_release
#include "scripting/button_hardware.cpp"
#undef button_hw_dispatch_click
#undef button_hw_dispatch_long_press
#undef button_hw_dispatch_release

namespace {
bool report(const int position, const EventType event, const bool prevented) {
    fbe_report_pending_exception();
    fbh_key_event(position, event, prevented ? 1 : 0);
    return prevented;
}
} // namespace

extern "C" {

bool button_hw_dispatch_click(const int position) {
    return report(position, CLICK, fbe_firmware_dispatch_click(position));
}

bool button_hw_dispatch_long_press(const int position) {
    return report(position, LONG_PRESS, fbe_firmware_dispatch_long_press(position));
}

bool button_hw_dispatch_release(const int position) {
    return report(position, RELEASE, fbe_firmware_dispatch_release(position));
}

} // extern "C"
