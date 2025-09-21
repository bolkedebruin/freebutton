#pragma once

#include <cstdint>

struct CRGB {
    uint8_t red;
    uint8_t green;
    uint8_t blue;

    CRGB() : red(0), green(0), blue(0) {}
    CRGB(const uint8_t r, const uint8_t g, const uint8_t b) : red(r), green(g), blue(b) {}
    CRGB(const uint32_t color) : red((color >> 16) & 0xFF), green((color >> 8) & 0xFF), blue(color & 0xFF) {}

    // FastLED compatibility methods
    void nscale8_video(const uint8_t scale) {
        red = (red * scale) >> 8;
        green = (green * scale) >> 8;
        blue = (blue * scale) >> 8;
    }

    uint32_t getRGB() const {
        return (static_cast<uint32_t>(red) << 16) | (static_cast<uint32_t>(green) << 8) | blue;
    }
};