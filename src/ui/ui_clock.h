//
// Created by Bolke de Bruin on 27/11/2025.
//

#ifndef FREEBUTTON_CLOCK_H
#define FREEBUTTON_CLOCK_H

#include "lvgl.h"

// Colors (matching original CSS variables)
#define COLOR_BKG              lv_color_hex(0x232946)  // --bkg
#define COLOR_DARK             lv_color_hex(0x121629)  // --dark
#define COLOR_PURPLE           lv_color_hex(0xb8c1ec)  // --purple
#define COLOR_PINK             lv_color_hex(0xeebbc3)  // --pink
#define COLOR_WHITE            lv_color_hex(0xfffffe)  // --white

typedef struct {
    lv_obj_t* container;
    lv_obj_t* scale;            // Scale widget for clock face
    lv_obj_t* date_label;       // Date display
    lv_obj_t* am_pm_label;      // AM/PM display

    // Clock hands as line indicators
    lv_obj_t* hour_hand;
    lv_obj_t* minute_hand;
    lv_obj_t* second_hand;

    lv_point_precise_t minute_hand_points[2];  // Only minute hand uses mutable points
} analog_clock_t;

// Global timer (accessible from Clock component)
extern lv_timer_t* g_timer;

// Function declarations
void clock_init(lv_obj_t* parent, int x, int y);

analog_clock_t* clock_create(
    lv_obj_t* parent,
    lv_coord_t x,
    lv_coord_t y
);
void clock_update(analog_clock_t* clock);
void clock_destroy(analog_clock_t* clock);
void clock_cleanup(void);
void clock_timer_cb(lv_timer_t* timer);

#endif //FREEBUTTON_CLOCK_H