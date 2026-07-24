//
// LED Hardware Abstraction Layer
// C-compatible interface to C++ NeoPixelController
//

#include <Arduino.h>
#include <esp_log.h>
#include "drivers/NeoPixel.h"
#include "led_hardware.h"

static const char* LTAG = "led_hw";

extern "C" {

// Get number of LEDs
int led_hw_get_count() {
    return NeoPixelController::NUMBER_OF_LEDS;
}

// Set LED color and show
int led_hw_set_color(int position, uint8_t r, uint8_t g, uint8_t b) {
    if (position < 0 || position >= NeoPixelController::NUMBER_OF_LEDS) {
        ESP_LOGW(LTAG, "set_color pos=%d OUT OF RANGE", position);
        return -1;  // Out of range
    }

    CRGB color = NeoPixelController::colorFromRGB(r, g, b);
    neopixels.setPixel(position, color);
    NeoPixelController::show();

    return 0;  // Success
}

} // extern "C"
