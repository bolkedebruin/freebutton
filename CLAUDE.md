# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build System and Commands

This is an ESP32 IoT firmware project using PlatformIO with dual framework support (Arduino + ESP-IDF).

### Common Development Commands
```bash
# Build firmware for ESP32
pio run

# Build and upload to device
pio run -e esp32 -t upload

# Monitor serial output
pio run -t monitor -e esp32

# Run native tests
pio test -e native

# Generate fonts (required before first build)
cd tools/fonts && ./generatefonts.sh

# Flash complete firmware
pio run -e esp32 -t upload
# Or using ESP-IDF tools: idf.py flash
```

### Build Environments
- `esp32`: Main target for ESP32-S3 hardware with LVGL UI, MQTT, WiFi
- `native`: Unit testing environment with Unity framework

### Version Management
```bash
# Get current firmware version
python3 tools/scripts/update_version.py --get

# Update firmware version
python3 tools/scripts/update_version.py "3.2.0"
```

## Architecture Overview

### Core System Architecture
The project implements a modular IoT button system with these key architectural components:

**Hardware Layer:**
- ESP32-S3 based with 16MB flash
- NeoPixel LED strips for visual feedback
- I2C sensor bus (BME280 environmental sensors)
- TFT display with LVGL UI framework
- Physical button inputs

**Firmware Architecture:**
- **Core Services** (`src/core/`): WiFi management, web server, hardware initialization, mDNS, LittleFS filesystem
- **MQTT System** (`src/mqtt/`): Broker registry supporting multiple MQTT connections, component subscription system, Home Assistant auto-discovery
- **Hardware Drivers** (`src/drivers/`): LED button controllers, sensor management, display management, device info, hardware scanning
- **UI Layer** (`src/ui/`): LVGL-based interface with progress bars, modals, font rendering
- **Utilities** (`src/utils/`, `src/json/`): Helper functions, async JSON parsing, color utilities

### Key Design Patterns

**MQTT Component System:**
- `MqttSubscriber` interface for components to receive MQTT messages
- `MqttBrokerRegistry` for managing multiple broker connections
- `HomeAssistantDiscovery` for automatic device registration
- Components register topics and receive callbacks on message delivery

**LED Button Management:**
- `LedButtonManager` coordinates multiple button controllers
- `LedButtonController` handles individual button LED states and MQTT integration
- `NeoPixelController` manages the actual LED hardware

**Hardware Abstraction:**
- `HardwareInitializer` orchestrates I2C bus setup and device detection
- `DisplayManager` and `LvglDisplay` abstract display operations
- `Sensors` namespace provides unified sensor reading interface

## Configuration and Hardware

### Pin Configuration
Hardware pin assignments are defined in `src/pins.h` and `src/config.h` (files not present in current checkout but referenced in main.cpp).

### Partition Table
Custom partition layout in `tools/partition_tables/button_v1.csv` allocates space for:
- Application firmware
- LittleFS filesystem
- Font storage partition

### Dependencies
Key library dependencies managed via PlatformIO:
- LVGL 9.4.0 for UI framework
- ArduinoJson 7.4.2 for JSON handling
- FastLED 3.7.7 for NeoPixel control
- Adafruit sensor libraries (BME280, MCP23008)
- ESPAsyncWebServer for web interface
- Custom LovyanGFX display drivers

## Development Workflow

### Font System
LVGL fonts with Material Design Icons are generated via `tools/fonts/generatefonts.sh`. This creates multiple font sizes and integrates icon fonts. Run before building if fonts are modified.

### Testing
Native tests run with Unity framework in the `native` environment. Tests are isolated from ESP32-specific code through preprocessor flags.

### Configuration Management
The firmware uses ESP-IDF's sdkconfig system for low-level configuration, with defaults in `sdkconfig.defaults` and environment-specific overrides in `sdkconfig.esp32`.

### JSON Processing Architecture
The project includes a sophisticated JSON processing system in `src/json/` for configuration management:

**Core Components:**
- **JsonWriter** (`src/json/JsonWriter.h`): RapidJSON-based streaming writer with RAII scoped objects/arrays
- **FilteredReaderHandler** (`src/json/FilteredReaderHandler.h`): SAX parser for selective JSON processing with path-based filtering
- **AsyncJsonParser** (`src/json/AsyncJsonParser.h`): Asynchronous JSON parser using FreeRTOS tasks for large documents
- **StreamWrappers** (`src/json/StreamWrappers.h`): Output stream adapters for AsyncWebServer and LittleFS File objects

**Usage Patterns:**
```cpp
// Streaming write with automatic scoping
rapidjson::Writer<FileOutputStream> writer(stream);
JsonWriter jsonWriter(writer);
auto doc = jsonWriter.Object();
auto items = doc.Array("displayitems");
auto item = items.Object();
item.Add("x", 100);

// Filtered SAX parsing for memory efficiency
FilteredReaderHandler handler;
handler.addFilter({"displayitems"}, [](const std::vector<std::string>& path, const std::string& json) {
    // Process only displayitems section
});
handler.parse(jsonData, length);
```

**Backwards Compatibility:**
- Supports path-based JSON parsing compatible with legacy ../button architecture
- Handles both old (`mqttbuttons`, `mqttdisplays`) and new naming conventions
- Memory-efficient for ESP32 constraints using SAX parsing and filtering

**ESP32 Firmware Constraints:**
- **No C++ Exceptions**: ESP32 firmware builds with `-fno-exceptions`, so avoid try/catch blocks
- **Memory Management**: Use RAII patterns and smart pointers instead of exception handling
- **Error Handling**: Use return values, std::optional, or error enums instead of throwing exceptions
- **JSON Parsing**: Never use RapidJSON Document (DOM) parsing as it explodes memory usage - always use SAX parsing with FilteredReaderHandler

### CI/CD Integration
GitHub Actions workflows handle:
- Automated building and testing
- Font generation
- Release packaging with version management
- Artifact upload for firmware binaries