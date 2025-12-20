//
// Created by Bolke de Bruin on 06/08/2025.
//

#ifndef CONFIG_H
#define CONFIG_H

#define MACRO_TO_STRING_HELPER(x) #x
#define MACRO_TO_STRING(x) MACRO_TO_STRING_HELPER(x)

#define BUTTON_COUNT 8
#define MODEL "Model 1 Version 9"
#define FIRMWARE "FreeButton 3.1.alpha1"
#define CDN "1.8"
#define GAMMA (2.2)
#define BASE_TOPIC "buttonplus/"
#define BUTTONEVENTSUBSCRIBELIMIT 8


#define PUBLICMQTTID "buttonplus"
#define PUBLICMQTT "mqtt://mqtt.button.plus"

#include <string>

constexpr unsigned long SENSOR_UPDATE_INTERVAL = 1000;

extern const std::string deviceId;

// Forward declarations
namespace freebutton {
    class ComponentManager;
}

// Global component manager
extern freebutton::ComponentManager componentManager;

#endif //CONFIG_H
