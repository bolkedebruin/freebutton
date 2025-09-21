#ifndef DISPLAYMANAGER_H
#define DISPLAYMANAGER_H

#include <LovyanGFX.hpp>
#include <memory>
#include <vector>
#include <Adafruit_MCP23008.h>

namespace ButtonPlus {

class LGFX_ESP32_SPI : public lgfx::LGFX_Device {
    //lgfx::Panel_ST7789 _panel_instance; // Use correct ST7789 driver like working implementation
    lgfx::Panel_ILI9341 _panel_instance; // Temporary: Use ILI9341 driver for testing
    lgfx::Bus_SPI _bus_instance;
    lgfx::Light_PWM _light_instance;

public:
    LGFX_ESP32_SPI(int cs_pin, int dc_pin, int backlight_pin) {
        // CS is controlled manually through MCP, so we set it to -1
        {
            auto cfg = _bus_instance.config();
            cfg.spi_host = SPI2_HOST;
            cfg.spi_mode = 0;
            cfg.freq_write = 80000000;
            cfg.freq_read = 16000000;
            cfg.spi_3wire = false;
            cfg.use_lock = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO; // Enable DMA like working implementation
            cfg.pin_sclk = 36;  // TFT_SCLK
            cfg.pin_mosi = 35;  // TFT_MOSI
            cfg.pin_miso = -1;  // TFT_MISO
            cfg.pin_dc = dc_pin;
            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }

        {
            auto cfg = _panel_instance.config();
            cfg.pin_cs = -1;  // CS controlled manually through MCP
            cfg.pin_rst = -1;
            cfg.pin_busy = -1;
            cfg.panel_width = 320;
            cfg.panel_height = 240;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            cfg.offset_rotation = 4;
            cfg.dummy_read_pixel = 16;
            cfg.dummy_read_bits = 1;
            cfg.readable = true;
            cfg.invert = false;  // ST7789 typically needs inversion ON
            cfg.rgb_order = false;  // BGR order like working implementation
            cfg.dlen_16bit = false;
            cfg.bus_shared = true;
            // CRITICAL: ST7789 memory dimensions - controller has 240x320 internal memory
            cfg.memory_width = 320;   // ST7789 internal memory width
            cfg.memory_height = 240;  // ST7789 internal memory height
            _panel_instance.config(cfg);
        }

        if (backlight_pin >= 0) {
            auto cfg = _light_instance.config();
            cfg.pin_bl = backlight_pin;
            cfg.invert = false;
            cfg.freq = 44100;
            cfg.pwm_channel = 7;
            _light_instance.config(cfg);
            _panel_instance.setLight(&_light_instance);
        }

        setPanel(&_panel_instance);
    }
};

class LGFX_ESP32_SPI_Small : public lgfx::LGFX_Device {
    lgfx::Panel_ST7735S _panel_instance;
    lgfx::Bus_SPI _bus_instance;

public:
    LGFX_ESP32_SPI_Small(int cs_pin, int dc_pin) {
        // CS is controlled manually through MCP, so we set it to -1
        {
            auto cfg = _bus_instance.config();
            cfg.spi_host = SPI2_HOST;
            cfg.spi_mode = 0;
            cfg.freq_write = 80000000;
            cfg.freq_read = 16000000;
            cfg.spi_3wire = false;
            cfg.use_lock = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO; // Enable DMA like working implementation
            cfg.pin_sclk = 36;  // TFT_SCLK
            cfg.pin_mosi = 35;  // TFT_MOSI
            cfg.pin_miso = -1;  // TFT_MISO
            cfg.pin_dc = dc_pin;
            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }

        {
            auto cfg = _panel_instance.config();
            cfg.pin_cs = -1;  // CS controlled manually through MCP
            cfg.pin_rst = -1;
            cfg.pin_busy = -1;
            cfg.panel_width = 80;
            cfg.panel_height = 160;
            //cfg.memory_width = 160;   // ST7789 internal memory width
            //cfg.memory_height = 240;  // ST7789 internal memory height

            cfg.offset_x = 24;
            //cfg.offset_y = 26;
            //cfg.offset_rotation = 1;
            cfg.dummy_read_pixel = 16;
            cfg.dummy_read_bits = 1;
            cfg.readable = true;
            cfg.invert = false;
            cfg.rgb_order = false;
            cfg.dlen_16bit = false;
            cfg.bus_shared = true;
            _panel_instance.config(cfg);
        }

        // No backlight control for small displays to avoid conflicts

        setPanel(&_panel_instance);
    }
};

class DisplayManager {
public:
    static constexpr int MAX_SMALL_DISPLAYS = 8;

private:
    std::unique_ptr<LGFX_ESP32_SPI> mainDisplay_;
    struct SmallDisplayEntry {
        std::unique_ptr<LGFX_ESP32_SPI_Small> display;
        int connector;
        int position;
    };
    std::vector<SmallDisplayEntry> smallDisplays_;
    std::vector<std::shared_ptr<Adafruit_MCP23008>> mcps_;
    int mainDisplayConnector_;

public:
    DisplayManager() = default;
    ~DisplayManager() = default;

    // Initialize displays based on detected hardware
    bool initialize(const std::vector<std::shared_ptr<Adafruit_MCP23008>>& mcps, int mainDisplayConnector);

    // Get main display (320x240)
    LGFX_ESP32_SPI* getMainDisplay() { return mainDisplay_.get(); }

    // Get small display by index
    LGFX_ESP32_SPI_Small* getSmallDisplay(int index) const;

    // Display control
    void showHelloWorld();
    void showDisplayNumbers();
    void showRotationTestPattern();
    void setBacklight(int displayIndex, uint8_t brightness);

    // Utility
    size_t getDisplayCount() const { return smallDisplays_.size() + (mainDisplay_ ? 1 : 0); }
    size_t getSmallDisplayCount() const { return smallDisplays_.size(); }

private:
    void initializeMainDisplay(std::shared_ptr<Adafruit_MCP23008> mcp, int connector);
    void initializeSmallDisplay(std::shared_ptr<Adafruit_MCP23008> mcp, int connector, int position);
    void selectMainDisplay() const;
    void selectSmallDisplay(int connector, int position) const;
    void deselectAllDisplays() const;
};

// Global display manager instance
extern std::unique_ptr<DisplayManager> g_displayManager;

// Global convenience functions
bool initializeDisplays(const std::vector<std::shared_ptr<Adafruit_MCP23008>>& mcps, int mainDisplayConnector);
DisplayManager& getDisplayManager();

} // namespace ButtonPlus

#endif // DISPLAYMANAGER_H