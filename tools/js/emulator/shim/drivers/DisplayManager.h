// Emulator shim: which displays exist. The main display on the LARGE
// connector, two key displays on every BAR connector, as the firmware's
// DisplayManager finds them.
#pragma once
// The firmware header's own guard: a relative include of it is skipped.
#ifndef DISPLAYMANAGER_H
#define DISPLAYMANAGER_H
#endif
#include <memory>
#include <vector>
#include "Adafruit_MCP23X08.h"

namespace FreeButton {
class DisplayManager {
public:
    struct SmallDisplay {
        int connector;
        int position;  // 0 = left, 1 = right
    };
    // DisplayManager::getSmallDisplayIndexByPosition: -1 for a key without a
    // display of its own (a key of the main display).
    int getSmallDisplayIndexByPosition(int buttonPosition) const;
    const std::vector<SmallDisplay>& smallDisplays() const { return smallDisplays_; }
    size_t getSmallDisplayCount() const { return smallDisplays_.size(); }
    // Whether a main display module was found (HardwareInitializer's tft320Bar).
    bool hasMainDisplay() const { return mainConnector_ >= 0; }
    void setMainConnector(const int connector) { mainConnector_ = connector; }
    void add(const SmallDisplay display) { smallDisplays_.push_back(display); }
    void clear() { smallDisplays_.clear(); }

private:
    std::vector<SmallDisplay> smallDisplays_;
    int mainConnector_ = -1;
};

DisplayManager& getDisplayManager();
bool initializeDisplays(const std::vector<std::shared_ptr<Adafruit_MCP23X08>>& mcps, int tft320Bar);
} // namespace FreeButton
