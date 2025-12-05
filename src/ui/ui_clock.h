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

    lv_point_precise_t minute_hand_points[2];  // Only minute hand uses mutable points
} analog_clock_t;


// Function declarations
void clock_init(lv_obj_t* parent, lv_coord_t x, lv_coord_t y, lv_coord_t width, lv_coord_t height, lv_align_t align);
analog_clock_t* clock_create(lv_obj_t* parent, lv_coord_t x, lv_coord_t y, lv_coord_t width, lv_coord_t height, lv_align_t align);

void clock_destroy();

#endif //FREEBUTTON_CLOCK_H