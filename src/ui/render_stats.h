// Render statistics (src/ui/render_stats.c): cycles and pixels in LVGL's
// software blending, by kind, and in mask generation. LVGL task only.
#ifndef RENDER_STATS_H
#define RENDER_STATS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RENDER_BLEND_FILL = 0,   /* solid colour, opaque, no mask (backgrounds) */
    RENDER_BLEND_FILL_OPA,   /* solid colour with opacity, no mask */
    RENDER_BLEND_FILL_MASK,  /* solid colour through an A8 mask (glyphs, A8 icons, AA edges) */
    RENDER_BLEND_IMAGE,      /* image, no mask (RGB565 rasters) */
    RENDER_BLEND_IMAGE_MASK, /* image through a mask (RGB565A8 rasters) */
    RENDER_BLEND_KINDS
} render_blend_kind_t;

typedef struct {
    uint32_t calls;
    uint32_t pixels;
    uint64_t cycles;
} render_stat_t;

typedef struct {
    render_stat_t blend[RENDER_BLEND_KINDS];
    render_stat_t mask;
} render_stats_t;

void render_stats_get(render_stats_t *out, bool reset);

#ifdef __cplusplus
}
#endif

#endif
