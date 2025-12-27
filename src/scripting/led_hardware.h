//
// LED Hardware Abstraction Layer - Header
// C-compatible interface to C++ NeoPixelController
//

#ifndef LED_HARDWARE_H
#define LED_HARDWARE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Get number of LEDs available
int led_hw_get_count();

// Set LED color at position
// Returns 0 on success, -1 on error
int led_hw_set_color(int position, uint8_t r, uint8_t g, uint8_t b);

#ifdef __cplusplus
}
#endif

#endif // LED_HARDWARE_H
