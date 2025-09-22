#pragma once

#include "FactoryButton.h"

namespace FreeButton {

// Simple callback-based approach - no persistent manager needed
void onDeviceInfoButtonEvent(ButtonEvent event);

// Initialize factory button for device info display
bool initializeDeviceInfoButton(int factoryButtonPin);

// Start/stop factory button monitoring
void startDeviceInfoButton();
void stopDeviceInfoButton();

} // namespace FreeButton