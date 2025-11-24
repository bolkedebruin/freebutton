//
// Created by Bolke de Bruin on 06/08/2025.
//
#include "NeoPixel.h"
#include "pins.h"

#define TEST_PATTERN_DELAY 200
#define FLASH_DURATION 500
#define GLOW_STEP_DELAY 30
#define EFFECT_TASK_STACK_SIZE 2000
#define EFFECT_TASK_PRIORITY 5

NeoPixelController::NeoPixelController(const int pin) : pin(pin), effectTaskHandle(nullptr) {}

/**
 * Initializes the NeoPixelController by setting up the specified number of LEDS (configured by NUMBER_OF_LEDS)
 * and configuring them for use with the WS2812B chipset in RGB order.
 * It sets the default brightness to MAX_BRIGHTNESS, clears the LED data buffer, and updates the LED strip to show the cleared state.
 */
void NeoPixelController::begin() {
    CFastLED::addLeds<WS2812B, NEOPIN, RGB>(leds, NUMBER_OF_LEDS, 0);
}

/**
 * Sets the brightness of the NeoPixel LED strip.
 *
 * @param brightness A value between 0 and 100 representing the desired brightness percentage.
 *                   The input value is constrained within this range before being mapped to the
 *                   hardware-specific brightness level.
 */
void NeoPixelController::setBrightness(uint8_t brightness) {
    brightness = constrain(brightness, 0, 100);
    const int scaledBrightness = map(brightness, 0, 100, 0, MAX_BRIGHTNESS);
    FastLED.setBrightness(scaledBrightness);
    FastLED.show();
}

/**
 * @brief Sets the color of a specific pixel in the NeoPixel strip.
 *
 * This function updates the color of the LED at the specified index in the
 * NeoPixel strip. If the given index is out of bounds, the function does nothing.
 *
 * @param index The index of the LED to set, must be within the range [0, NUMBER_OF_LEDS-1].
 * @param color The CRGB color value to set the LED to.
 */
void NeoPixelController::setPixel(const int index, const CRGB color) {
    if (index >= 0 && index < NUMBER_OF_LEDS) {
        leds[index] = color;
    }
}

void NeoPixelController::setAllPixels(const CRGB color) {
    fill_solid(leds, NUMBER_OF_LEDS, color);
}

/**
 * @brief Displays the current LED data on the NeoPixel strip.
 *
 * This function calls the FastLED's show() method to update the
 * NeoPixel strip with the current data stored in the led array.
 */
void NeoPixelController::show() {
    FastLED.show();
}

/**
 * Computes the index of a pixel in a row based on the specified parameters.
 *
 * @param row The row number for which the pixel index is being calculated.
 * @param isRight A boolean flag indicating whether the pixel is on the right side.
 * @param isWall A boolean flag indicating whether the pixel is on the wall.
 * @return The calculated index of the pixel.
 */
int NeoPixelController::pixelIndex(const int row, const bool isRight, const bool isWall) {
    const int start = row * 4;
    return start + (isRight ? 2 : 0) + (isWall ? 1 : 0);
}

/**
 * Converts a hexadecimal color value to a CRGB color object.
 *
 * @param hexColor The color value represented as a 32-bit hexadecimal number.
 * @return A CRGB object representing the color corresponding to the hexadecimal value.
 */
CRGB NeoPixelController::colorFromHex(uint32_t hexColor) {
    return {hexColor};
}

/**
 * Converts RGB values to a CRGB color object.
 *
 * @param red The red component of the color.
 * @param green The green component of the color.
 * @param blue The blue component of the color.
 * @return A CRGB object representing the combined RGB color.
 */
CRGB NeoPixelController::colorFromRGB(uint8_t red, uint8_t green, uint8_t blue) {
    return {red, green, blue};
}

/**
 * Converts a color code string to a CRGB object.
 *
 * The input string can be in several formats:
 * - Hexadecimal color starting with "0x" or "0X"
 * - Hexadecimal color starting with "#"
 * - Direct hex or decimal string representation
 *
 * @param colorString The color code string to be converted.
 * @return A CRGB object representing the color.
 */
CRGB NeoPixelController::colorFromString(const std::string_view colorString) {
    if (colorString.empty()) {
        return {0};
    }

    // Handle 0x/0X prefix
    size_t startPos = 0;
    int base = 10;

    if (colorString.length() >= 2) {
        if (colorString.substr(0, 2) == "0x" || colorString.substr(0, 2) == "0X") {
            startPos = 2;
            base = 16;
        } else if (colorString[0] == '#') {
            startPos = 1;
            base = 16;
        }
    }

    // Check if we have any characters left after prefix
    if (startPos >= colorString.length()) {
        return {0};
    }

    std::string_view numPart = colorString.substr(startPos);

    // Verify all characters are valid for the given base
    bool isValid = true;
    for (char c : numPart) {
        if (base == 16) {
            isValid &= isxdigit(c);
        } else {
            isValid &= isdigit(c);
        }
    }

    if (!isValid) {
        return {0};
    }

    // Convert to unsigned long long manually for hex
    unsigned long long value = 0;
    for (char c : numPart) {
        value *= base;
        if (isdigit(c)) {
            value += c - '0';
        } else if (base == 16) {
            c = tolower(c);
            if (c >= 'a' && c <= 'f') {
                value += 10 + (c - 'a');
            }
        }
    }

    return CRGB(value);
}


/**
 * Runs a sequential test pattern red, green, blue
 */
void NeoPixelController::runTestPattern() {
    setAllPixels(colorFromRGB(100, 0, 0));
    show();
    delay(TEST_PATTERN_DELAY);
    setAllPixels(colorFromRGB(0, 100, 0));
    show();
    delay(TEST_PATTERN_DELAY);
    setAllPixels(colorFromRGB(0, 0, 100));
    show();
    delay(TEST_PATTERN_DELAY);
    setAllPixels(CRGB::Black);
    show();
    delay(TEST_PATTERN_DELAY);
}

/**
 * @brief Flashes all the NeoPixels by turning them white and then turning them black.
 *
 * This function sets all the LEDs to white, displays the updated colors,
 * waits for a predefined flash duration, and then sets all the LEDs to black
 * and updates the display again.
 */
void NeoPixelController::flash() {
    setAllPixels(CRGB::White);
    show();
    delay(FLASH_DURATION);
    setAllPixels(CRGB::Black);
    show();
}

/**
 * @brief Starts a lighting effect on the NeoPixel strip.
 *
 * The supported effects are:
 * - "glow": A glowing effect where the LEDs gradually change brightness.
 * - "ambient": An ambient color effect based on the provided color parameter.
 * - "flash": A flashing effect that repeatedly turns the LEDs on and off.
 *
 * @param effectName The name of the effect to start. Supported values are "glow", "ambient", and "flash".
 * @param parameter An optional parameter for the effect. For the "ambient" effect, this should be a string
 *                  representing the color (e.g., "#FF0000" for red).
 */
void NeoPixelController::startEffect(const char* effectName, const char* parameter) {
    stopEffect();  // Ensure any running effect is stopped first

    if (strcmp(effectName, "glow") == 0) {
        xTaskCreate(effectTask, "glowEffect", EFFECT_TASK_STACK_SIZE, (void*)this, EFFECT_TASK_PRIORITY, &effectTaskHandle);
    } else if (strcmp(effectName, "ambient") == 0 && parameter != nullptr) {
        const CRGB color = colorFromString(parameter);
        runAmbientEffect(color);
    } else if (strcmp(effectName, "flash") == 0) {
        runFlashEffect();
    }
}

/**
 * @brief Stops any currently running LED effect and resets the NeoPixel strip.
 */
void NeoPixelController::stopEffect() {
    if (effectTaskHandle != nullptr) {
        vTaskDelete(effectTaskHandle);
        effectTaskHandle = nullptr;
    }
    setAllPixels(CRGB::Black);
    show();
}

/**
 * @brief Executes the glow effect on the NeoPixel strip.
 *
 * This function creates a continuous glowing effect by
 * incrementally increasing the brightness of all LEDs from 0
 * to MAX_BRIGHTNESS and then decreasing it back to 0. The glow
 * effect cycles infinitely.
 *
 * The function alternates between increasing and decreasing
 * the red color component of the LEDs. After setting the color
 * of all LEDs, it updates the strip by calling the show()
 * method and waits for a short period defined by GLOW_STEP_DELAY.
 */
void NeoPixelController::runGlowEffect() {
    while (true) {
        for (int i = 0; i < MAX_BRIGHTNESS; i++) {
            setAllPixels(colorFromRGB(i, 0, 0));
            show();
            delay(GLOW_STEP_DELAY);
        }
        for (int i = MAX_BRIGHTNESS; i > 0; i--) {
            setAllPixels(colorFromRGB(i, 0, 0));
            show();
            delay(GLOW_STEP_DELAY);
        }
    }
}

void NeoPixelController::runAmbientEffect(CRGB color) {
    setAllPixels(color);
    show();
}

void NeoPixelController::runFlashEffect() {
    flash();
}

void NeoPixelController::effectTask(void* pvParameters) {
    auto* controller = static_cast<NeoPixelController*>(pvParameters);
    controller->runGlowEffect();
}
