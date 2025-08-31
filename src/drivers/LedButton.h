//
// Created by Bolke de Bruin on 31/08/2025.
//

#ifndef FREEBUTTON_LEDBUTTON_H
#define FREEBUTTON_LEDBUTTON_H

#include "Adafruit_MCP23008.h"

class LedButton {
    LedButton() : pin(0), mpc(nullptr), history(0x0001) {} ;

    Adafruit_MCP23008 *mpc;
    uint8_t pin;

    bool pressed() const;
    bool buttonDown() const;
    bool released() const;

private:
    uint8_t history;

};
#endif //FREEBUTTON_LEDBUTTON_H