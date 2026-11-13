//
// Image binding - C interface to the main-display images
// (components/ImageItem.h) for the JS API fb.main.image (freebutton_device.c).
//
// An image is addressed by its index in config.json "images"; image_hw_find()
// resolves a config name. Any task (scripts, JS engine lock held); nothing
// here takes the LVGL lock: the setters only store what to show.
//

#ifndef IMAGE_BINDING_H
#define IMAGE_BINDING_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Index of the image with this config name (len bytes), -1 when none.
int image_hw_find(const char* name, size_t len);

// Whether there is an image at index.
bool image_hw_exists(int index);

// Show an icon (a config.json "icons" name) or an images-bucket key; empty
// shows nothing. Returns 0 on success, -1 when there is no image at index,
// -2 for an invalid icon name ([A-Za-z0-9_-], max 31) or a key longer than
// 96 bytes.
int image_hw_set_icon(int index, const char* name, size_t len);
int image_hw_set_src(int index, const char* key, size_t len);

#ifdef __cplusplus
}
#endif

#endif // IMAGE_BINDING_H
