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

#define CLOCK_SIZE 200
#define CLOCK_CONTAINER_SIZE 260
#define CLOCK_CENTER (CLOCK_SIZE / 2)

static analog_clock_t* g_clock = NULL;
lv_timer_t* g_timer = NULL;

static void update_clock_hands(analog_clock_t* clock, uint8_t hour, uint8_t minute, uint8_t second);
static void update_time_labels(analog_clock_t* clock);
void clock_timer_cb(lv_timer_t* timer);
static void get_current_time(int* hour, int* minute, int* second);

void clock_init(lv_obj_t* parent, int x, int y)
{
    if (g_clock != NULL) {
        return;
    }

    g_clock = clock_create(parent, x, y);
    if (g_clock != NULL) {
        // Create timer after lock is released
        g_timer = lv_timer_create(clock_timer_cb, 250, g_clock);
    }
}

analog_clock_t* clock_create(lv_obj_t* parent, lv_coord_t x, lv_coord_t y)
{
    static analog_clock_t clock_instance;
    analog_clock_t* clock = &clock_instance;

    clock->container = lv_obj_create(parent);
    lv_obj_set_size(clock->container, 150, 150);
    lv_obj_set_style_bg_opa(clock->container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(clock->container, 0, 0);
    lv_obj_set_style_clip_corner(clock->container, false, 0);  // Disable clipping to prevent border clipping
    lv_obj_set_scrollbar_mode(clock->container, LV_SCROLLBAR_MODE_OFF);
    // Remove all padding and margins
    lv_obj_set_style_pad_all(clock->container, 0, 0);
    lv_obj_center(clock->container);

    // Create scale
    clock->scale = lv_scale_create(clock->container);
    lv_scale_set_mode(clock->scale, LV_SCALE_MODE_ROUND_INNER);
    lv_obj_set_size(clock->scale, 150, 150);
    lv_obj_set_style_bg_opa(clock->scale, LV_OPA_100, 0);
    lv_obj_set_style_bg_color(clock->scale, lv_color_white(), 0);  // NS white background
    lv_obj_set_style_radius(clock->scale, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(clock->scale, true, 0);
    // Remove all padding and margins from scale
    lv_obj_set_style_pad_all(clock->scale, 0, 0);
    // Position scale at container origin (0,0) since they're the same size
    lv_obj_set_pos(clock->scale, 0, 0);

    lv_scale_set_label_show(clock->scale, true);
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

    clock->minute_hand = lv_line_create(clock->container);
    lv_line_set_points_mutable(clock->minute_hand, clock->minute_hand_points, 2);
    lv_obj_set_style_line_width(clock->minute_hand, 4, 0);
    lv_obj_set_style_line_rounded(clock->minute_hand, true, 0);
    lv_obj_set_style_line_color(clock->minute_hand, lv_color_hex(0x02060a), 0);  // NS blue
    lv_obj_center(clock->minute_hand);

    clock->hour_hand = lv_line_create(clock->container);
    lv_obj_set_style_line_width(clock->hour_hand, 6, 0);
    lv_obj_set_style_line_rounded(clock->hour_hand, true, 0);
    lv_obj_set_style_line_color(clock->hour_hand, lv_color_hex(0x02060a), 0);  // NS blue
    lv_obj_center(clock->hour_hand);

    clock->second_hand = lv_line_create(clock->container);
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
    // Debug: Log container and scale positions and sizes
    ESP_LOGI(TAG, "Container size: %dx%d, pos: (%d,%d)",
             lv_obj_get_width(clock->container), lv_obj_get_height(clock->container),
             lv_obj_get_x(clock->container), lv_obj_get_y(clock->container));
    ESP_LOGI(TAG, "Scale size: %dx%d, pos: (%d,%d)",
             lv_obj_get_width(clock->scale), lv_obj_get_height(clock->scale),
             lv_obj_get_x(clock->scale), lv_obj_get_y(clock->scale));
    lv_area_t content_area;
    lv_obj_get_content_coords(clock->container, &content_area);
    ESP_LOGI(TAG, "Container content area: (%d,%d) to (%d,%d)",
             content_area.x1, content_area.y1, content_area.x2, content_area.y2);

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

    int hour_12 = hour % 12;
    if (hour_12 == 0) hour_12 = 12; // 0 becomes 12, 12 stays 12

    // Debug: Log current point coordinates before update
    ESP_LOGI(TAG, "Before update - minute_hand_points[0]: (%ld, %ld), minute_hand_points[1]: (%ld, %ld)",
             clock->minute_hand_points[0].x, clock->minute_hand_points[0].y,
             clock->minute_hand_points[1].x, clock->minute_hand_points[1].y);

    int hour_position = hour_12 * 5 + (minute / 12);
    ESP_LOGI(TAG, "Hour hand: hour_12=%d, minute=%d, position=%d", hour_12, minute, hour_position);

    lv_scale_set_line_needle_value(clock->scale, clock->second_hand, 65, second);  // Long second hand
    lv_scale_set_line_needle_value(clock->scale, clock->minute_hand, 55, minute);  // Medium minute hand
    lv_scale_set_line_needle_value(clock->scale, clock->hour_hand, 35, hour_position);  // Short hour hand

    // Debug: Log coordinates after update
    ESP_LOGI(TAG, "After update - minute_hand_points[0]: (%ld, %ld), minute_hand_points[1]: (%ld, %ld)",
             clock->minute_hand_points[0].x, clock->minute_hand_points[0].y,
             clock->minute_hand_points[1].x, clock->minute_hand_points[1].y);

    ESP_LOGI(TAG, "Updating clock with time %02d:%02d:%02d", hour, minute, second);

    //update_clock_hands(clock, hour, minute, second);
    //update_time_labels(clock);
}

void clock_destroy(analog_clock_t* clock)
{
    if (!clock) return;

    // Delete LVGL objects (scale automatically deletes child hands)
    if (clock->scale) {
        lv_obj_del(clock->scale);
    }

    // Free the dynamically allocated clock instance
    free(clock);
}

void clock_cleanup(void)
{
    if (g_timer != NULL) {
        lv_timer_del(g_timer);
        g_timer = NULL;
    }

    if (g_clock != NULL) {
        clock_destroy(g_clock);
        g_clock = NULL;
    }
}


static void update_time_labels(analog_clock_t* clock)
{
    time_t now;
    struct tm* timeinfo;
    char date_str[32];
    char am_pm_str[8];

    time(&now);
    timeinfo = localtime(&now);

    strftime(date_str, sizeof(date_str), "%m/%d", timeinfo);
    strftime(am_pm_str, sizeof(am_pm_str), "%p", timeinfo);

    lv_label_set_text(clock->date_label, date_str);
    lv_label_set_text(clock->am_pm_label, am_pm_str);
}

void clock_timer_cb(lv_timer_t* timer)
{
    ESP_LOGD(TAG, "Timer callback triggered");
    analog_clock_t* clock = (analog_clock_t*)lv_timer_get_user_data(timer);
    if (clock) {
        // Use standard update - locking should be handled at a higher level
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
