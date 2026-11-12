// Raster images (icons) from the media store's `images` bucket, drawn by LVGL
// straight from memory-mapped flash: no decode, no heap for the pixels
// (docs/ICONS.md).
//
// The web IDE rasterizes the SVGs a config uses (its `icons` table) at every
// size the config asks for, in LVGL's own binary image format, and uploads
// them to the `images` bucket. The device never parses SVG.
//
// Object format = LVGL 9 .bin: a 12-byte lv_image_header_t (little endian:
// magic 0x19, cf, flags 0, w, h, stride, reserved 0) followed by the pixels.
//   cf 0x0E A8        coverage only, stride = w. Single-colour artwork; LVGL
//                     tints it at draw time (style image_recolor), so a
//                     colour change needs no new raster.
//   cf 0x14 RGB565A8  RGB565 plane (stride = w * 2, little endian), then an
//                     A8 plane (stride = w). Anything else.
//   cf 0x12 RGB565    opaque, stride = w * 2.
// File size must be exactly 12 + the pixel bytes; anything else is refused.
//
// Keys: icon rasters are named iconRasterKey(name, w, h) =
// "ico-<name>-<w>x<h>.bin" (the IDE owns and garbage-collects that prefix);
// other keys in the bucket are user files and are never touched.
#pragma once

#include <cstddef>
#include <cstdint>

#include <lvgl.h>

namespace freebutton::ui {

// Longest icon name in the config's `icons` table ([A-Za-z0-9_-]).
inline constexpr size_t kIconNameMax = 31;

// "ico-<name>-<w>x<h>.bin" into out; false when the name is not a valid icon
// name or the key does not fit.
bool iconRasterKey(char* out, size_t cap, const char* name, uint16_t w, uint16_t h);

// LVGL task only, LVGL lock held. Maps the raster (refcounted per key: every
// item showing the same key shares one mapping and one descriptor) and
// returns a descriptor for lv_image_set_src(), or nullptr when the key does
// not exist or is not a valid raster. Release exactly once per acquire, after
// the image object no longer shows it.
const lv_image_dsc_t* mediaImageAcquire(const char* key);
void mediaImageRelease(const lv_image_dsc_t* dsc);

// Bumped whenever the images bucket changes (S3 PUT/DELETE). Items that show
// a raster remember the generation they resolved at; when it differs they
// re-acquire (a replaced file stays mapped until its last release).
uint32_t mediaImageGeneration();
// Any task (the S3 hooks): bump the generation and ask the LVGL task to
// re-render (freebutton::ui::requestPendingRender()).
void mediaImageNotifyChanged();

}  // namespace freebutton::ui
