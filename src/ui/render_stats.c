// Render statistics for POST /api/bench (docs/PERFORMANCE.md): CPU cycles
// and pixels in LVGL's software blending and mask generation, the work a
// SIMD implementation could take over. The functions are wrapped at link
// time (-Wl,--wrap=..., platformio.ini), so LVGL itself is unchanged.
// LVGL task only (no locking).
#include "render_stats.h"

#include "esp_cpu.h"
#include "lvgl.h"
#include "src/draw/sw/blend/lv_draw_sw_blend_private.h"
#include "src/draw/sw/lv_draw_sw_mask.h"

static render_stats_t stats;

void __real_lv_draw_sw_blend(lv_draw_task_t *t, const lv_draw_sw_blend_dsc_t *dsc);
void __wrap_lv_draw_sw_blend(lv_draw_task_t *t, const lv_draw_sw_blend_dsc_t *dsc);
lv_draw_sw_mask_res_t __real_lv_draw_sw_mask_apply(void *masks[], lv_opa_t *mask_buf, int32_t abs_x, int32_t abs_y,
                                                   int32_t len);
lv_draw_sw_mask_res_t __wrap_lv_draw_sw_mask_apply(void *masks[], lv_opa_t *mask_buf, int32_t abs_x, int32_t abs_y,
                                                   int32_t len);

void __wrap_lv_draw_sw_blend(lv_draw_task_t *t, const lv_draw_sw_blend_dsc_t *dsc) {
    const uint32_t c0 = esp_cpu_get_cycle_count();
    __real_lv_draw_sw_blend(t, dsc);
    const uint32_t c = esp_cpu_get_cycle_count() - c0;
    const uint32_t px = dsc->blend_area ? (uint32_t)lv_area_get_size(dsc->blend_area) : 0;
    const bool masked = dsc->mask_buf != NULL && dsc->mask_res != LV_DRAW_SW_MASK_RES_FULL_COVER;
    render_blend_kind_t k;
    if (dsc->src_buf == NULL) {
        k = masked ? RENDER_BLEND_FILL_MASK : (dsc->opa >= LV_OPA_MAX ? RENDER_BLEND_FILL : RENDER_BLEND_FILL_OPA);
    } else {
        k = masked ? RENDER_BLEND_IMAGE_MASK : RENDER_BLEND_IMAGE;
    }
    stats.blend[k].calls++;
    stats.blend[k].pixels += px;
    stats.blend[k].cycles += c;
}

lv_draw_sw_mask_res_t __wrap_lv_draw_sw_mask_apply(void *masks[], lv_opa_t *mask_buf, int32_t abs_x, int32_t abs_y,
                                                   int32_t len) {
    const uint32_t c0 = esp_cpu_get_cycle_count();
    const lv_draw_sw_mask_res_t r = __real_lv_draw_sw_mask_apply(masks, mask_buf, abs_x, abs_y, len);
    stats.mask.calls++;
    stats.mask.pixels += (uint32_t)len;
    stats.mask.cycles += esp_cpu_get_cycle_count() - c0;
    return r;
}

void render_stats_get(render_stats_t *out, bool reset) {
    if (out) *out = stats;
    if (reset) lv_memzero(&stats, sizeof stats);
}
