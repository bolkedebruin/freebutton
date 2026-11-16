// Emulator shim: the firmware opens files through a filesystem that shares
// the SPI bus with the display. Here: the host's files.
#pragma once
// The firmware header's own guard: a relative include of it is skipped.
#ifndef BUSAWAREFILESYSTEM_H
#define BUSAWAREFILESYSTEM_H
#endif
#include "FS.h"

namespace FreeButton {
using BusAwareFile = ::File;

class BusAwareFileSystem {
public:
    // An invalid file when the host has none of that name.
    BusAwareFile open(const char* path, const char* mode = "r");
};

BusAwareFileSystem& getBusAwareFileSystem();
} // namespace FreeButton
