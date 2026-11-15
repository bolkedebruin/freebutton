// Emulator shim for src/scripting/device_hardware.cpp: it only asks whether
// config.json has lists / images (fb.main.list, fb.main.image). The device
// state itself lives in the host (web/src/js/emulator/device-state.ts).
#pragma once
#include <stddef.h>
#include "../../fbh.h"
namespace freebutton {
class ConfigManager {
public:
    const void* findList(const size_t index) const { return fbh_list_count(static_cast<int>(index)) >= 0 ? this : nullptr; }
    const void* findImage(const size_t index) const { return fbh_image_exists(static_cast<int>(index)) ? this : nullptr; }
};
} // namespace freebutton
