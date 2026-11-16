// Emulator shim: the factory button (device info / credentials screen) is not
// part of the virtual device.
#pragma once
namespace FreeButton {
inline bool initializeDeviceInfoButton(int pin) { (void)pin; return true; }
inline void startDeviceInfoButton() {}
} // namespace FreeButton
