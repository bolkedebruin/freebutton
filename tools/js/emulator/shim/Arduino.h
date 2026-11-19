// Emulator shim (tools/js/emulator): what the firmware sources compiled into
// fb-emulator.wasm use of <Arduino.h>. Time is the emulator's clock.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h> /* as the real Arduino.h: malloc / free for the firmware sources */
#include <string>
#include "WString.h"
#include "esp_system.h"

#define HIGH 1
#define LOW 0
#define INPUT 0x01
#define OUTPUT 0x03
#define INPUT_PULLUP 0x05
#define FILE_READ "r"

typedef bool boolean;

#ifdef __cplusplus
extern "C" {
#endif
// Milliseconds since the device booted (tools/js/emulator/runner.c).
unsigned long millis(void);
#ifdef __cplusplus
}
#endif
static inline void delay(unsigned long ms) { (void)ms; }
