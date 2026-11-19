//
// Small rules of the config loader that need no hardware: the native tests
// build this header (test/test_config_logic).
//
#ifndef FREEBUTTON_CONFIGLOGIC_H
#define FREEBUTTON_CONFIGLOGIC_H

#include <ArduinoJson.h>

namespace freebutton::configlogic {

// A broker's "hassdiscovery": absent (or null) = Home Assistant discovery is
// on; otherwise the value as a boolean, as ArduinoJson converts it (false
// and 0 are off; true, a number that is not 0 and a string are on).
// `value | true` is not the same: it gives true for every value that is no
// JSON boolean, so "hassdiscovery": 0 would turn discovery on.
inline bool hassDiscovery(const ArduinoJson::JsonVariantConst value) {
    return value.isNull() ? true : value.as<bool>();
}

// A "buttons" entry is a button only with a "position" (which key it is).
// Without one the entry is skipped whole: no button, no topics, no name.
inline bool buttonHasPosition(const ArduinoJson::JsonVariantConst entry) {
    return !entry["position"].isNull();
}

} // namespace freebutton::configlogic

#endif // FREEBUTTON_CONFIGLOGIC_H
