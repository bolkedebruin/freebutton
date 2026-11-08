//
// Button Hardware Abstraction Layer - Header
// C-compatible interface to C++ Button components
//

#ifndef BUTTON_HARDWARE_H
#define BUTTON_HARDWARE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Callback types for button events. Return true to prevent the default
// action (MQTT publishing for this press), i.e. the JS handler returned false.
typedef bool (*ButtonClickCallback)(int position);
typedef bool (*ButtonLongPressCallback)(int position);
typedef bool (*ButtonReleaseCallback)(int position);

// Set label for button at position
// Returns 0 on success, -1 on error
int button_hw_set_label(int position, const char* label);

// Set top label for button at position
// Returns 0 on success, -1 on error
int button_hw_set_top_label(int position, const char* topLabel);

// What the button at position shows as label / top label (last runtime text,
// else the configured one), NUL-terminated into out. Returns 0, or -1 when
// there is no such button (out is then "").
int button_hw_get_label(int position, char* out, size_t capacity);
int button_hw_get_top_label(int position, char* out, size_t capacity);

// Get button position count
int button_hw_get_count();

// Register event callbacks
// These will be called from ButtonManager when events occur
void button_hw_register_click_callback(int position, ButtonClickCallback callback);
void button_hw_register_long_press_callback(int position, ButtonLongPressCallback callback);
void button_hw_register_release_callback(int position, ButtonReleaseCallback callback);

// Called by ButtonManager to dispatch events to JavaScript.
// Return true when the handler prevented the default action (returned false):
// the caller must then skip MQTT publishing (configured topics + HA event).
bool button_hw_dispatch_click(int position);
bool button_hw_dispatch_long_press(int position);
bool button_hw_dispatch_release(int position);

#ifdef __cplusplus
}
#endif

#endif // BUTTON_HARDWARE_H
