// Emulator shim for src/scripting/device_hardware.cpp: the module topology
// (which connector has a bar, which the main display) comes from the host.
#pragma once
#include <optional>
#include "../../fbh.h"
namespace FreeButton {
struct Scanner {
    std::optional<int> getConnectorType(const int connector) const {
        const int type = fbh_connector_type(connector);
        if (type < 0) return std::nullopt;
        return type;
    }
};
struct HardwareInitializer {
    Scanner scanner;
    Scanner& getScanner() { return scanner; }
    int getTft320Bar() const { return fbh_main_connector(); }
};
inline HardwareInitializer& getHardwareInitializer() {
    static HardwareInitializer instance;
    return instance;
}
} // namespace FreeButton
