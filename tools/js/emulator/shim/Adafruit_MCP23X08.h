// Emulator shim for src/drivers/PushButton.h: no port expander; the key
// states come from the host as the GPIO byte PushButton::updateState reads.
#pragma once
#include <stdint.h>
#include "esp_log.h"
class Adafruit_MCP23X08 {};
