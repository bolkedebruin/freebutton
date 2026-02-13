//
// Device Hardware Abstraction Layer - Header
// C-compatible interface to HardwareInitializer for device topology discovery
//

#ifndef DEVICE_HARDWARE_H
#define DEVICE_HARDWARE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Get connector type: 0=EMPTY, 1=BAR, 2=LARGE
int device_hw_get_connector_type(int connector);

// Get connector number of LARGE display, or -1 if none
int device_hw_get_main_connector(void);

// Calculate NeoPixel strip index from button position (1-based) and wall flag
int device_hw_led_index(int buttonPos, int isWall);

// Initialize the fb object tree in a JSContext (built entirely in C, no JS parsing)
void device_hw_init_fb(void* ctx);

// Index of js_fb_device_proxy in the stdlib c_function_table.
// Must match its position in mqjs_stdlib.c js_c_function_decl[].
// "bound" = 0, "fb_proxy" = 1 (when CONFIG_CLASS_EXAMPLE is not defined)
#define FB_PROXY_FUNC_IDX 1

#ifdef __cplusplus
}
#endif

#endif // DEVICE_HARDWARE_H
