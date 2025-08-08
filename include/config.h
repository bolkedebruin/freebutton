//
// Created by Bolke de Bruin on 06/08/2025.
//

#ifndef CONFIG_H
#define CONFIG_H

#define MACRO_TO_STRING_HELPER(x) #x
#define MACRO_TO_STRING(x) MACRO_TO_STRING_HELPER(x)

#define BUTTON_COUNT 8
#define FIRMWARE "3.1.alpha1"
#define CDN "1.8"
#define GAMMA (2.2)
#define BASE_TOPIC "buttonplus/"
#define BUTTONEVENTSUBSCRIBELIMIT 8


#define PUBLICMQTTID "buttonplus"
#define PUBLICMQTT "mqtt://mqtt.button.plus"

#include <string>
#include <unordered_map>

constexpr unsigned long SENSOR_UPDATE_INTERVAL = 1000;

extern const std::string deviceId;

// helpers for backwards compatibility
extern std::unordered_map<uint8_t, std::string> idToBrokerName;

#endif //CONFIG_H
