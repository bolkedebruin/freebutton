//
// Button Hardware Abstraction Layer - Header
// C-compatible interface to C++ Button components
//

#ifndef BUTTON_HARDWARE_H
#define BUTTON_HARDWARE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Callback types for button events
typedef void (*ButtonClickCallback)(int position);
typedef void (*ButtonLongPressCallback)(int position);
typedef void (*ButtonReleaseCallback)(int position);

// Set label for button at position
// Returns 0 on success, -1 on error
int button_hw_set_label(int position, const char* label);

// Set top label for button at position
// Returns 0 on success, -1 on error
int button_hw_set_top_label(int position, const char* topLabel);

// Get button position count
int button_hw_get_count();

// Register event callbacks
// These will be called from ButtonManager when events occur
void button_hw_register_click_callback(int position, ButtonClickCallback callback);
void button_hw_register_long_press_callback(int position, ButtonLongPressCallback callback);
void button_hw_register_release_callback(int position, ButtonReleaseCallback callback);

// Called by ButtonManager to dispatch events to JavaScript
void button_hw_dispatch_click(int position);
void button_hw_dispatch_long_press(int position);
void button_hw_dispatch_release(int position);

#ifdef __cplusplus
}
#endif

#endif // BUTTON_HARDWARE_H
