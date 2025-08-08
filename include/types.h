//
// Created by Bolke de Bruin on 06/08/2025.
//

#ifndef TYPES_H
#define TYPES_H

enum EventType : uint8_t {  	// keep in sync with web app
    CLICK = 0,
    LONG_PRESS = 1,
    RELEASE = 2,
    PAGE = 6,

    LED_BLUE = 8,
    LED_RED = 9,
    LED_GREEN = 10,
    LABEL = 11,
    TOP_LABEL = 12,
    LED_RGB = 13,
    LED = 14,
    DISPLAY_VALUE = 15,
    DISPLAY_LABEL = 16,
    DISPLAY_UNIT = 17,

    SENSOR_VALUE = 18,

    SET_PAGE = 20,

    LARGE_DISPLAY_BRIGHTNESS = 24,
    MINI_DISPLAY_BRIGHTNESS = 25,
    BRIGHTNESS = 26,
    LED_BRIGHTNESS = 27,

    LED_FRONT_RGB = 28,
    LED_WALL_RGB = 29,
    WALL_LED_EFFECT = 30,

    LABEL_SET = 31,
    TOP_LABEL_SET = 32,

    DISPLAY_VALUE_SET = 33,
    DISPLAY_LABEL_SET = 34,
    DISPLAY_UNIT_SET = 35,

    LED_RGB_SET = 36,
    LED_ON = 38,
    LED_ON_SET = 42,
    LED_BRIGHTNESS_SET = 43,
};

enum LedType : uint8_t {
    FRONT = 0,
    BACK = 1,
    BOTH = 2,
    NONE = 100,
};

constexpr char CLICK_PAYLOAD[] = "{\"event_type\":\"click\"}";
constexpr char LONG_PRESS_PAYLOAD[] = "{\"event_type\":\"longpress\"}";
constexpr char RELEASE_PAYLOAD[] = "{\"event_type\":\"release\"}";

#endif //TYPES_H
