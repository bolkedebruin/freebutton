//
// Created by Bolke de Bruin on 06/08/2025.
//

#ifndef NEOPIXEL_H
#define NEOPIXEL_H

#include <FastLED.h>
#include <string_view>

class NeoPixelController {
public:
    static const int NUMBER_OF_LEDS = 12;
    static const int MAX_BRIGHTNESS = 255;

    explicit NeoPixelController(int pin);
    void begin();

    void setBrightness(uint8_t brightness);
    void setPixel(int index, CRGB color);
    void setAllPixels(CRGB color);
    void show();

    int pixelIndex(int row, bool isRight, bool isWall);
    CRGB colorFromHex(uint32_t hexColor);
    CRGB colorFromRGB(uint8_t red, uint8_t green, uint8_t blue);
    CRGB colorFromString(std::string_view colorString);

    void runTestPattern();
    void flash();

    void startEffect(const char* effectName, const char* parameter = nullptr);
    void stopEffect();

private:
    CRGB leds[NUMBER_OF_LEDS];
    int pin;
    TaskHandle_t effectTaskHandle;

    void runGlowEffect();
    void runAmbientEffect(CRGB color);
    void runFlashEffect();

    static void effectTask(void* pvParameters);
};

#endif //NEOPIXEL_H
