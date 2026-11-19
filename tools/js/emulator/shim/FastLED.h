// Emulator shim: the part of FastLED that src/drivers/NeoPixel.cpp uses. The
// strip is an array; show() hands the pixels that changed to the host.
#pragma once
#include <stdint.h>
#include <cstring>
#include "Arduino.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

struct CRGB {
    uint8_t r = 0, g = 0, b = 0;
    constexpr CRGB() = default;
    constexpr CRGB(const uint8_t red, const uint8_t green, const uint8_t blue) : r(red), g(green), b(blue) {}
    // 0xRRGGBB
    constexpr CRGB(const uint32_t code)
        : r(static_cast<uint8_t>(code >> 16)), g(static_cast<uint8_t>(code >> 8)), b(static_cast<uint8_t>(code)) {}
    static const CRGB Black;
    static const CRGB White;
    static const CRGB Red;
    static const CRGB Green;
    static const CRGB Blue;
};
inline constexpr CRGB CRGB::Black{0, 0, 0};
inline constexpr CRGB CRGB::White{255, 255, 255};
inline constexpr CRGB CRGB::Red{255, 0, 0};
inline constexpr CRGB CRGB::Green{0, 255, 0};
inline constexpr CRGB CRGB::Blue{0, 0, 255};

struct WS2812B {};
enum EOrder { RGB = 0, GRB = 1 };

class CFastLED {
public:
    template <class Chipset, int Pin, EOrder Order>
    static void addLeds(CRGB* leds, const int count, const int offset = 0) {
        (void)offset;
        attach(leds, count);
    }
    static void attach(CRGB* leds, int count);
    // The strip's brightness scales what is shown, not the pixel values.
    void setBrightness(const uint8_t brightness) { brightness_ = brightness; }
    void show();

private:
    uint8_t brightness_ = 255;
};
extern CFastLED FastLED;

static inline void fill_solid(CRGB* leds, const int count, const CRGB color) {
    for (int i = 0; i < count; ++i) leds[i] = color;
}
template <class T, class L, class H>
constexpr T constrain(const T value, const L low, const H high) {
    return value < low ? static_cast<T>(low) : value > high ? static_cast<T>(high) : value;
}
static inline long map(const long x, const long inMin, const long inMax, const long outMin, const long outMax) {
    return (x - inMin) * (outMax - outMin) / (inMax - inMin) + outMin;
}
