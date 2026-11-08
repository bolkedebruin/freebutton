//
// Created by Bolke de Bruin on 27/11/2025.
//

#include "ui_clock.h"
#include <time.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <esp_log.h>

static const char* TAG = "clock";

static analog_clock_t* g_clock = NULL;
lv_timer_t* g_timer = NULL;

void clock_timer_cb(lv_timer_t* timer);
static void get_current_time(int* hour, int* minute, int* second);
static void lv_scale_set_line_needle_value_with_tail(lv_obj_t * scale, lv_obj_t * needle_line,
                                                      int32_t needle_length, int32_t tail_length,
                                                      int32_t value);

void clock_init(lv_obj_t* parent, lv_coord_t x, lv_coord_t y, lv_coord_t width, lv_coord_t height, const lv_align_t align)
{
    if (g_clock != NULL) {
        return;
    }

    g_clock = clock_create(parent, x, y, width, height, align);
    g_timer = lv_timer_create(clock_timer_cb, 250, g_clock);
}

analog_clock_t* clock_create(lv_obj_t* parent, const lv_coord_t x, const lv_coord_t y, const lv_coord_t width, const lv_coord_t height, const lv_align_t align)
{
    static analog_clock_t clock_instance;
    analog_clock_t* clock = &clock_instance;

    clock->container = lv_obj_create(parent);
    lv_obj_set_size(clock->container, width, height);
    lv_obj_set_style_bg_opa(clock->container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(clock->container, 0, 0);
    lv_obj_set_style_clip_corner(clock->container, false, 0);  // Disable clipping to prevent border clipping
    lv_obj_set_scrollbar_mode(clock->container, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_all(clock->container, 0, 0);

    if (align == LV_ALIGN_CENTER) {
        lv_obj_center(clock->container);
    } else {
        lv_obj_set_pos(clock->container, x, y);
    }

    // Create scale
    clock->scale = lv_scale_create(clock->container);
    lv_scale_set_mode(clock->scale, LV_SCALE_MODE_ROUND_INNER);
    lv_obj_set_size(clock->scale, width, height);
    lv_obj_set_style_bg_opa(clock->scale, LV_OPA_100, 0);
    lv_obj_set_style_bg_color(clock->scale, lv_color_white(), 0);  // NS white background
    lv_obj_set_style_radius(clock->scale, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(clock->scale, true, 0);
    lv_obj_set_style_pad_all(clock->scale, 0, 0);

    // Position scale at container origin (0,0) since they're the same size
    lv_obj_set_pos(clock->scale, 0, 0);

    lv_scale_set_label_show(clock->scale, false);
    lv_scale_set_total_tick_count(clock->scale, 61);
    lv_scale_set_major_tick_every(clock->scale, 5);

    static const char * hour_ticks[] = {"12", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", NULL};
    lv_scale_set_text_src(clock->scale, hour_ticks);

    static lv_style_t indicator_style;
    lv_style_init(&indicator_style);

    /* Label style properties */
    lv_style_set_text_font(&indicator_style, LV_FONT_DEFAULT);
    lv_style_set_text_color(&indicator_style, lv_color_hex(0x0d2034));  // Light blue-gray

    /* Major tick properties */
    lv_style_set_line_color(&indicator_style, lv_color_hex(0x0d2034));  // Light blue-gray
    lv_style_set_length(&indicator_style, 12); /* tick length */
    lv_style_set_line_width(&indicator_style, 3); /* tick width */
    lv_obj_add_style(clock->scale, &indicator_style, LV_PART_INDICATOR);

    /* Minor tick properties */
    static lv_style_t minor_ticks_style;
    lv_style_init(&minor_ticks_style);
    lv_style_set_line_color(&minor_ticks_style, lv_color_hex(0x0d2034));  // Light blue-gray
    lv_style_set_length(&minor_ticks_style, 6); /* tick length */
    lv_style_set_line_width(&minor_ticks_style, 1); /* tick width */
    lv_obj_add_style(clock->scale, &minor_ticks_style, LV_PART_ITEMS);

    /* Main line properties */
    static lv_style_t main_line_style;
    lv_style_init(&main_line_style);
    lv_style_set_arc_color(&main_line_style, lv_color_hex(0x0d2034));  // Light blue-gray border
    lv_style_set_arc_width(&main_line_style, 2);
    lv_obj_add_style(clock->scale, &main_line_style, LV_PART_MAIN);

    lv_scale_set_range(clock->scale, 0, 60);

    lv_scale_set_angle_range(clock->scale, 360);
    lv_scale_set_rotation(clock->scale, 270);


    // Initialize minute_hand_points to current time position to prevent huge invalidation areas
    int hour, minute, second;
    get_current_time(&hour, &minute, &second);

    // Create hour hand with mutable points
    clock->hour_hand = lv_line_create(clock->container);
    lv_line_set_points_mutable(clock->hour_hand, clock->hour_hand_points, 2);
    lv_obj_set_style_line_width(clock->hour_hand, 6, 0);
    lv_obj_set_style_line_rounded(clock->hour_hand, true, 0);
    lv_obj_set_style_line_color(clock->hour_hand, lv_color_hex(0x02060a), 0);  // NS blue
    lv_obj_center(clock->hour_hand);

    // Create minute hand with mutable points
    clock->minute_hand = lv_line_create(clock->container);
    lv_line_set_points_mutable(clock->minute_hand, clock->minute_hand_points, 2);
    lv_obj_set_style_line_width(clock->minute_hand, 4, 0);
    lv_obj_set_style_line_rounded(clock->minute_hand, true, 0);
    lv_obj_set_style_line_color(clock->minute_hand, lv_color_hex(0x02060a), 0);  // NS blue
    lv_obj_center(clock->minute_hand);

    // Create second hand with mutable points
    clock->second_hand = lv_line_create(clock->container);
    lv_line_set_points_mutable(clock->second_hand, clock->second_hand_points, 2);
    lv_obj_set_style_line_width(clock->second_hand, 2, 0);
    lv_obj_set_style_line_rounded(clock->second_hand, true, 0);
    lv_obj_set_style_line_color(clock->second_hand, lv_color_hex(0xE30613), 0);  // NS red
    lv_obj_center(clock->second_hand);

    // Initial needle positions will be set by clock_update() - don't set during creation
    // This prevents invalidation area issues with uninitialized mutable points

    /*
    // Create date label
    clock->date_label = lv_label_create(clock->container);
    lv_obj_set_style_text_color(clock->date_label, COLOR_WHITE, 0);
    lv_obj_set_style_text_font(clock->date_label, &lv_font_montserrat_14, 0);
    lv_obj_align(clock->date_label, LV_ALIGN_TOP_RIGHT, -10, 50);
    lv_label_set_text(clock->date_label, "");

    // Create AM/PM label
    clock->am_pm_label = lv_label_create(clock->container);
    lv_obj_set_style_text_color(clock->am_pm_label, COLOR_PINK, 0);
    lv_obj_set_style_text_font(clock->am_pm_label, &lv_font_montserrat_14, 0);
    lv_obj_align(clock->am_pm_label, LV_ALIGN_TOP_RIGHT, -10, 70);
    lv_label_set_text(clock->am_pm_label, "");
    */

    ESP_LOGI(TAG, "Clock created successfully at (%d, %d)", x, y);

    return clock;
}

void clock_update(analog_clock_t* clock)
{
    if (clock == NULL) {
        ESP_LOGW(TAG, "Clock update called with null pointer");
        return;
    }

    int hour, minute, second;
    get_current_time(&hour, &minute, &second);

    // Static variables to track last update
    static int last_hour = -1;
    static int last_minute = -1;
    static int last_second = -1;

    // Only update if minute or hour changed
    if (hour == last_hour && minute == last_minute && last_second == second) {
        return;  // No change, skip expensive drawing
    }

    last_hour = hour;
    last_minute = minute;
    last_second = second;

    int hour_12 = hour % 12;  // Keep in 0-11 range (0=12 o'clock, 1=1 o'clock, etc)

    // Calculate hour hand position with smooth movement
    // Each hour = 5 scale units, add fractional position based on minutes with rounding
    const int hour_position = hour_12 * 5 + (minute * 5 + 30) / 60;

    // Use LVGL-based needle positioning with tail support
    lv_scale_set_line_needle_value_with_tail(clock->scale, clock->second_hand, 65, 15, second);      // 65px forward, 15px tail
    lv_scale_set_line_needle_value_with_tail(clock->scale, clock->minute_hand, 55, 12, minute);      // 55px forward, 12px tail
    lv_scale_set_line_needle_value_with_tail(clock->scale, clock->hour_hand, 35, 8, hour_position);  // 35px forward, 8px tail

    ESP_LOGD(TAG, "Updating clock with time %02d:%02d:%02d", hour, minute, second);
}

// Destroys the single clock created by clock_init(). Caller must hold the
// LVGL lock. The header declares this without parameters (and C++ calls it
// that way), so it must not take the clock as an argument: it used to, read a
// garbage pointer, and free()d the static clock instance.
void clock_destroy(void)
{
    if (g_timer != NULL) {
        lv_timer_delete(g_timer);
        g_timer = NULL;
    }
    if (g_clock != NULL) {
        // The container owns the scale, hands and labels.
        if (g_clock->container) {
            lv_obj_delete(g_clock->container);
        }
        memset(g_clock, 0, sizeof(*g_clock));  // static instance: never free()
        g_clock = NULL;
    }
}

static void update_time_labels(analog_clock_t* clock)
{
    time_t now;
    char date_str[32];
    char am_pm_str[8];

    time(&now);
    struct tm *timeinfo = localtime(&now);

    strftime(date_str, sizeof(date_str), "%m/%d", timeinfo);
    strftime(am_pm_str, sizeof(am_pm_str), "%p", timeinfo);

    lv_label_set_text(clock->date_label, date_str);
    lv_label_set_text(clock->am_pm_label, am_pm_str);
}

void clock_timer_cb(lv_timer_t* timer)
{
    ESP_LOGD(TAG, "Timer callback triggered");
    analog_clock_t* clock = lv_timer_get_user_data(timer);
    if (clock) {
        clock_update(clock);
    } else {
        ESP_LOGW(TAG, "Clock pointer is null in timer callback");
    }
}

static void get_current_time(int* hour, int* minute, int* second)
{
    time_t now;

    time(&now);
    struct tm *timeinfo = localtime(&now);

    *hour = timeinfo->tm_hour;
    *minute = timeinfo->tm_min;
    *second = timeinfo->tm_sec;
}

/**
 * Extended version of lv_scale_set_line_needle_value that adds tail/counterweight support
 * Based on LVGL's lv_scale_set_line_needle_value implementation
 * @param scale The scale object
 * @param needle_line The line object representing the needle
 * @param needle_length Length of needle extending forward from center
 * @param tail_length Length of tail extending backward from center
 * @param value The value to display on the scale
 */
static void lv_scale_set_line_needle_value_with_tail(lv_obj_t * scale, lv_obj_t * needle_line,
                                                      int32_t needle_length, int32_t tail_length,
                                                      int32_t value)
{
    // Get scale properties using public API
    int32_t scale_width = lv_obj_get_style_width(scale, LV_PART_MAIN);
    int32_t scale_height = lv_obj_get_style_height(scale, LV_PART_MAIN);
    int32_t rotation = lv_scale_get_rotation(scale);
    uint32_t angle_range = lv_scale_get_angle_range(scale);
    int32_t range_min = lv_scale_get_range_min_value(scale);
    int32_t range_max = lv_scale_get_range_max_value(scale);

    // Position the needle line
    lv_obj_align(needle_line, LV_ALIGN_TOP_LEFT, 0, 0);

    // Calculate actual needle length (clamp to scale radius)
    int32_t actual_needle_length;
    if(needle_length >= scale_width / 2) {
        actual_needle_length = scale_width / 2;
    }
    else if(needle_length >= 0) {
        actual_needle_length = needle_length;
    }
    else if(needle_length + scale_width / 2 < 0) {
        actual_needle_length = 0;
    }
    else {
        actual_needle_length = scale_width / 2 + needle_length;
    }

    // Calculate actual tail length (clamp if needed)
    int32_t actual_tail_length;
    if(tail_length >= scale_width / 2) {
        actual_tail_length = scale_width / 2;
    }
    else if(tail_length >= 0) {
        actual_tail_length = tail_length;
    }
    else {
        actual_tail_length = 0;
    }

    // Calculate angle based on value and scale range
    int32_t angle;
    if(value < range_min) {
        angle = 0;
    }
    else if(value > range_max) {
        angle = angle_range;
    }
    else {
        angle = angle_range * (value - range_min) / (range_max - range_min);
    }

    // Calculate needle endpoint using LVGL's trigonometry functions
    // LVGL uses fixed-point math with LV_TRIGO_SHIFT
    int32_t needle_length_x = (actual_needle_length * lv_trigo_cos(rotation + angle)) >> LV_TRIGO_SHIFT;
    int32_t needle_length_y = (actual_needle_length * lv_trigo_sin(rotation + angle)) >> LV_TRIGO_SHIFT;

    // Calculate tail endpoint (opposite direction)
    int32_t tail_length_x = (actual_tail_length * lv_trigo_cos(rotation + angle + 180)) >> LV_TRIGO_SHIFT;
    int32_t tail_length_y = (actual_tail_length * lv_trigo_sin(rotation + angle + 180)) >> LV_TRIGO_SHIFT;

    // Get or allocate mutable points array
    lv_point_precise_t * needle_line_points = lv_line_get_points_mutable(needle_line);

    // Set the points: [0] = tail end, [1] = needle tip
    needle_line_points[0].x = scale_width / 2 + tail_length_x;
    needle_line_points[0].y = scale_height / 2 + tail_length_y;
    needle_line_points[1].x = scale_width / 2 + needle_length_x;
    needle_line_points[1].y = scale_height / 2 + needle_length_y;

    // Refresh the line
    lv_line_set_points_mutable(needle_line, needle_line_points, 2);
}
