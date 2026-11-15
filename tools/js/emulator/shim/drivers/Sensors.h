// Emulator shim for src/scripting/sensor_binding.cpp: the same shape as the
// firmware's FreeButton::sensorDevices, with sensors whose samples come from
// the host.
#pragma once
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include "../../fbh.h"
namespace FreeButton {
class SensorDevice {
public:
    SensorDevice(const int id_, const char* name_, const char* deviceClass_, const char* unit_, const bool online_)
        : name_storage(name_), class_storage(deviceClass_), unit_storage(unit_), id(id_),
          name(name_storage), deviceClass(class_storage), unitOfMeasurement(unit_storage), online(online_) {}
    float sample() const { return fbh_sensor_sample(id); }

private:
    const std::string name_storage, class_storage, unit_storage;

public:
    const int id;
    const std::string_view name;
    const std::string_view deviceClass;
    const std::string_view unitOfMeasurement;
    bool online;
};
extern std::map<int, std::shared_ptr<SensorDevice>> sensorDevices;
} // namespace FreeButton
