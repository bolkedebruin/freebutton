#ifndef DISPLAYMANAGER_H
#define DISPLAYMANAGER_H

#include <LovyanGFX.hpp>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>
#include <Adafruit_MCP23X08.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Forward declaration to avoid circular dependency
namespace FreeButton {
    class BusAwareFileSystem;
}

namespace FreeButton {

// ---------------------------------------------------------------------------
// SPI write clocks. DMA stays enabled on both panel types (owner requirement).
// All panels share SPI2 and run on ONE clock.
// NOTE: LovyanGFX < 1.2.28 computed the divider against a fixed 80 MHz base,
// so on ESP32-S3 (Arduino) the real SCLK was up to 2x the request: a 40 MHz
// setting ran at ~80 MHz (lovyan03/LovyanGFX#899). Since 1.2.28 (we pin
// 1.3.0) the request is the real clock, so 40 MHz here really is 40 MHz.
// ---------------------------------------------------------------------------

// Main panel (ILI9341). The datasheet twc >= 100 ns (10 MHz) is conservative;
// ILI9341 panels are commonly run at 40 MHz (Bodmer/TFT_eSPI#1941: reliable up
// to 65 MHz). The real limit here is signal integrity over the connector. A
// lower init clock (init != run) is possible via initWithClockRamp() but is
// not verified on hardware, so both are 40 MHz.
inline constexpr uint32_t kMainPanelInitSpiHz = 40000000;
inline constexpr uint32_t kMainPanelRunSpiHz  = 40000000;

// Mini panels (ST7735S). Datasheet tSCYCW >= 66 ns (about 15 MHz); the signal
// crosses bar connectors and cables. 40 MHz (half the old 80 MHz) is verified
// to display correctly; if mini panels still corrupt in the field, try a lower
// value, but test it on hardware together with the main panel (shared SPI2).
inline constexpr uint32_t kSmallPanelSpiHz = 40000000;

// Longest delay the mini-panel register refresh (self-heal) waits after one
// init command. The ST7735S needs 5 ms after SLPOUT before the next command;
// the full boot init waits 500 ms there, which the refresh does not need
// because the panel is normally already awake.
inline constexpr uint32_t kSmallPanelRefreshMaxDelayMs = 10;

// ST7735S that can re-send its register setup without a reset, for the
// mini-panel self-heal in LvglDisplay.
struct Panel_ST7735S_Refreshable : public lgfx::Panel_ST7735S {
    // Re-sends the panel's own init command lists, except CMD_SWRESET, with
    // each post-command delay capped at maxDelayMs. Then re-sends COLMOD,
    // MADCTL and INVON/INVOFF as LGFX_Device::init does after the list.
    // None of these commands touch frame memory, so there is no visible clear.
    // The caller must have selected the panel's CS (MCP23008) first.
    void refreshRegisters(uint32_t maxDelayMs) {
        startWrite(true);
        for (uint8_t listno = 0;; ++listno) {
            const uint8_t* cmds = getInitCommands(listno);
            if (cmds == nullptr) {
                break;
            }
            resendListWithoutReset(cmds, maxDelayMs);
        }
        update_madctl();
        setInvert(getInvert());
        endWrite();
    }

private:
    void resendListWithoutReset(const uint8_t* addr, uint32_t maxDelayMs) {
        for (;;) {
            const uint8_t cmd = *addr++;
            uint8_t num = *addr++;
            if (cmd == 0xFF && num == 0xFF) {
                break;
            }
            const bool hasDelay = (num & CMD_INIT_DELAY) != 0;
            num &= static_cast<uint8_t>(~CMD_INIT_DELAY);

            if (cmd == CMD_SWRESET) {
                addr += num + (hasDelay ? 1 : 0);  // skip args and delay byte
                continue;
            }

            writeCommand(cmd, 1);
            _bus->flush();
            for (; num > 0; --num) {
                writeData(*addr++, 1);
                _bus->flush();
            }
            if (hasDelay) {
                const uint8_t raw = *addr++;
                uint32_t ms = (raw == 255) ? 500 : raw;  // same rule as command_list
                if (ms > maxDelayMs) {
                    ms = maxDelayMs;
                }
                lgfx::delay(ms);  // the same delay command_list uses
            }
        }
    }
};

class LGFX_ESP32_SPI : public lgfx::LGFX_Device {
    // Stock ILI9341 init. Do NOT prefix a software reset (CMD_SWRESET): on
    // this hardware it leaves the main panel blank (verified 2026-11-07).
    lgfx::Panel_ILI9341 _panel_instance;
    lgfx::Bus_SPI _bus_instance;
    lgfx::Light_PWM _light_instance;

public:
    LGFX_ESP32_SPI(int cs_pin, const int dc_pin, const int backlight_pin) {
        // CS is controlled manually through MCP, so we set it to -1
        {
            auto cfg = _bus_instance.config();
            cfg.spi_host = SPI2_HOST;
            cfg.spi_mode = 0;
            cfg.freq_write = kMainPanelInitSpiHz;  // raised to kMainPanelRunSpiHz by initWithClockRamp()
            cfg.freq_read = 16000000;
            cfg.spi_3wire = false;
            cfg.use_lock = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk = 36;  // TFT_SCLK
            cfg.pin_mosi = 35;  // TFT_MOSI
            cfg.pin_miso = -1;  // TFT_MISO: not wired
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
            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits = 1;
            cfg.readable = false;   // no MISO, so panel reads are not possible
            cfg.invert = false;
            cfg.rgb_order = true;   // RGB order to fix inverted colors
            cfg.dlen_16bit = false;
            cfg.bus_shared = false;
            cfg.memory_width = 320;
            cfg.memory_height = 240;
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

    // Runs init() (stock init list + clear) at kMainPanelInitSpiHz, then
    // raises the write clock to kMainPanelRunSpiHz. Bus_SPI::setClock only
    // stores the value; the next beginTransaction() recomputes the divider.
    bool initWithClockRamp() {
        _bus_instance.setClock(kMainPanelInitSpiHz);
        const bool ok = init();
        _bus_instance.setClock(kMainPanelRunSpiHz);
        return ok;
    }
};

class LGFX_ESP32_SPI_Small : public lgfx::LGFX_Device {
    Panel_ST7735S_Refreshable _panel_instance;
    lgfx::Bus_SPI _bus_instance;

public:
    LGFX_ESP32_SPI_Small(int cs_pin, const int dc_pin) {
        // CS is controlled manually through MCP, so we set it to -1
        {
            auto cfg = _bus_instance.config();
            cfg.spi_host = SPI2_HOST;
            cfg.spi_mode = 0;
            cfg.freq_write = kSmallPanelSpiHz;  // see the ST7735S limit above
            cfg.freq_read = 16000000;
            cfg.spi_3wire = false;
            cfg.use_lock = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk = 36;  // TFT_SCLK
            cfg.pin_mosi = 35;  // TFT_MOSI
            cfg.pin_miso = -1;  // TFT_MISO: not wired
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
            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits = 1;
            cfg.readable = false;   // no MISO, so panel reads are not possible
            cfg.invert = false;
            cfg.rgb_order = false;
            cfg.dlen_16bit = false;
            cfg.bus_shared = false;
            _panel_instance.config(cfg);
        }

        // No backlight control for small displays to avoid conflicts

        setPanel(&_panel_instance);
    }

    // Self-heal: re-send the panel register setup without SWRESET and without
    // clearing frame memory. The caller selects CS first and repaints after.
    void refreshPanelRegisters() {
        _panel_instance.refreshRegisters(kSmallPanelRefreshMaxDelayMs);
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
    std::vector<std::shared_ptr<Adafruit_MCP23X08>> mcps_;
    int mainDisplayConnector_ = -1;

    // Chip-select state of the MCP23008s.
    //
    // csMutex_ guards olatShadow_ and serialises every CS write. At runtime
    // all CS writes come from the LVGL task (flush callbacks and the self-heal
    // timer, both under the LVGL lock); at boot they come from setup() before
    // the LVGL task and the button task start. csMutex_ makes that safe even
    // if a new caller appears. It is a leaf lock: nothing else is taken while
    // it is held except TwoWire's internal bus lock.
    //
    // The button task reads the same MCPs (ButtonManager::mcpMutex_, GPIO
    // register only). That needs no shared lock: TwoWire (arduino-esp32
    // 3.3.x) holds its own mutex for each whole transaction, including the
    // write-then-read of readGPIO(), so transactions never interleave. Writing
    // OLAT does not change input pins, so reads stay correct.
    SemaphoreHandle_t csMutex_ = nullptr;
    // Last value written to each MCP's OLAT register, indexed like mcps_.
    // Only the CS bits are driven by this class; other bits are preserved.
    mutable std::vector<uint8_t> olatShadow_;
    // CS writes that failed after every retry (I2C NACK / bus error / timeout).
    mutable std::atomic<uint32_t> csWriteFailures_{0};
    // Individual failed attempts that were retried (includes recovered ones).
    mutable std::atomic<uint32_t> csWriteRetries_{0};

public:
    DisplayManager();
    ~DisplayManager();
    DisplayManager(const DisplayManager&) = delete;
    DisplayManager& operator=(const DisplayManager&) = delete;

    // Initialize displays based on detected hardware
    bool initialize(const std::vector<std::shared_ptr<Adafruit_MCP23X08>>& mcps, int mainDisplayConnector);

    // Get main display (320x240)
    LGFX_ESP32_SPI* getMainDisplay() const { return mainDisplay_.get(); }

    // Get small display by index
    LGFX_ESP32_SPI_Small* getSmallDisplay(int index) const;

    // Get connector and position for small display
    bool getSmallDisplayInfo(int index, int& connector, int& position) const;

    // Convert button position to small display index
    // Returns -1 if the button position is on a connector without small displays (e.g., large display)
    int getSmallDisplayIndexByPosition(int buttonPosition) const;

    // Display control
    void showHelloWorld() const;
    void showDisplayNumbers() const;
    void showRotationTestPattern() const;
    void setBacklight(int displayIndex, uint8_t brightness) const;

    // Utility
    size_t getDisplayCount() const { return smallDisplays_.size() + (mainDisplay_ ? 1 : 0); }
    size_t getSmallDisplayCount() const { return smallDisplays_.size(); }

    // Display selection (public for LVGL integration).
    // Each MCP23008 gets one OLAT write (one I2C transaction) that is checked
    // and retried. They return false when a write failed after all retries;
    // then the CS state is unknown and the caller must not start an SPI
    // transfer. A select first deselects every other MCP and only then
    // selects the target, so two panels are never selected at the same time.
    bool selectMainDisplay() const;
    bool selectSmallDisplay(int connector, int position) const;
    bool deselectAllDisplays() const;

    // Diagnostics: CS writes that failed after all retries, and failed
    // attempts that were retried. Both only increase.
    uint32_t getCsWriteFailureCount() const { return csWriteFailures_.load(std::memory_order_relaxed); }
    uint32_t getCsWriteRetryCount() const { return csWriteRetries_.load(std::memory_order_relaxed); }

private:
    void initializeMainDisplay(int connector);
    void initializeSmallDisplay(int connector, int position);
    void initChipSelectShadow();
    // Writes the CS bits of every MCP: all deselected, except selectMask
    // (active-low CS bits) on selectConnector. selectConnector < 0 = none.
    bool applyChipSelect(int selectConnector, uint8_t selectMask) const;
    // One checked OLAT write with retries. Caller holds csMutex_.
    bool writeOlatLocked(size_t connector, uint8_t value) const;
};

// Global display manager instance
extern std::unique_ptr<DisplayManager> g_displayManager;

// Global convenience functions
bool initializeDisplays(const std::vector<std::shared_ptr<Adafruit_MCP23X08>>& mcps, int mainDisplayConnector);
DisplayManager& getDisplayManager();

} // namespace FreeButton

#endif // DISPLAYMANAGER_H