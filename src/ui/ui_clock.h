//
// Created by Bolke de Bruin on 27/11/2025.
//

#ifndef FREEBUTTON_CLOCK_H
#define FREEBUTTON_CLOCK_H

#include "lvgl.h"

typedef struct {
    lv_obj_t* container;
    lv_obj_t* scale;            // Scale widget for clock face
    lv_obj_t* date_label;       // Date display
    lv_obj_t* am_pm_label;      // AM/PM display

    // Clock hands as line indicators
    lv_obj_t* hour_hand;
    lv_obj_t* minute_hand;
    lv_obj_t* second_hand;

    // Mutable points for all hands (point[0] = tail, point[1] = tip)
    lv_point_precise_t hour_hand_points[2];
    lv_point_precise_t minute_hand_points[2];
    lv_point_precise_t second_hand_points[2];
} analog_clock_t;


// Function declarations
// Creates the single clock and its timer. Caller holds the LVGL lock.
// false when a clock already exists (only one clock is drawn).
bool clock_init(lv_obj_t* parent, lv_coord_t x, lv_coord_t y, lv_coord_t width, lv_coord_t height, lv_align_t align);
analog_clock_t* clock_create(lv_obj_t* parent, lv_coord_t x, lv_coord_t y, lv_coord_t width, lv_coord_t height, lv_align_t align);

void clock_destroy(void);


#endif //FREEBUTTON_CLOCK_H