#include "DisplayManager.h"
#include "BusAwareFileSystem.h"
#include "pins.h"
#include <Adafruit_BusIO_Register.h>
#include <esp_log.h>

namespace FreeButton {

static auto TAG = "DisplayManager";

// Global instance
std::unique_ptr<DisplayManager> g_displayManager;

namespace {

// Every CS line the firmware drives on an MCP23008 (active low). BAR
// connectors use LEFT and RIGHT; the LARGE connector uses MCP320_CS (the same
// pin as LEFT). The MCP ignores OLAT bits of input pins, so driving the whole
// mask high on every MCP is safe.
constexpr uint8_t kAllCsMask = static_cast<uint8_t>(
    (1u << MCP160_CS_LEFT) | (1u << MCP160_CS_RIGHT) | (1u << MCP320_CS));

// A failed OLAT write is retried this many times (1 + kCsWriteRetries
// attempts in total) before it counts as a failure.
constexpr int kCsWriteRetries = 3;
constexpr uint32_t kCsWriteRetryDelayMs = 1;

// Log the 1st failure and then every Nth, so a dead connector cannot flood
// the log from the flush path.
constexpr uint32_t kCsFailureLogEvery = 64;

// Adafruit_MCP23XXX keeps its Adafruit_I2CDevice in a protected member, and
// its digitalWrite()/writeGPIO() return void. A derived class may form a
// pointer to that protected member, and applying it to the base object is
// well-defined C++. This gives access to Adafruit_BusIO_Register::write(),
// which returns the TwoWire::endTransmission() result as a bool.
struct McpI2cAccess : Adafruit_MCP23XXX {
    static Adafruit_I2CDevice* device(Adafruit_MCP23XXX& mcp) {
        return mcp.*(&McpI2cAccess::i2c_dev);
    }
};

// RAII holder for csMutex_.
class CsLock {
public:
    explicit CsLock(SemaphoreHandle_t m) : m_(m) {
        if (m_) xSemaphoreTake(m_, portMAX_DELAY);
    }
    ~CsLock() {
        if (m_) xSemaphoreGive(m_);
    }
    CsLock(const CsLock&) = delete;
    CsLock& operator=(const CsLock&) = delete;

private:
    SemaphoreHandle_t m_;
};

uint8_t csMaskForPosition(const int position) {
    return static_cast<uint8_t>(1u << ((position == 0) ? MCP160_CS_LEFT : MCP160_CS_RIGHT));
}

} // namespace

DisplayManager::DisplayManager()
    : csMutex_(xSemaphoreCreateMutex()) {
    if (!csMutex_) {
        ESP_LOGE(TAG, "Failed to create chip-select mutex");
    }
}

DisplayManager::~DisplayManager() {
    if (csMutex_) {
        vSemaphoreDelete(csMutex_);
    }
}

bool DisplayManager::initialize(const std::vector<std::shared_ptr<Adafruit_MCP23X08>>& mcps, int mainDisplayConnector) {
    ESP_LOGI(TAG, "Initializing displays...");

    mcps_ = mcps;
    mainDisplayConnector_ = mainDisplayConnector;

    initChipSelectShadow();

    // Initialize main display if connector specified
    if (mainDisplayConnector >= 0 && mainDisplayConnector < mcps_.size() && mcps_[mainDisplayConnector]) {
        initializeMainDisplay(mainDisplayConnector);
    }

    // Initialize small displays on remaining connectors
    int displayIndex = 0;
    for (int connector = 0; connector < mcps_.size(); ++connector) {
        if (connector == mainDisplayConnector || !mcps_[connector]) {
            continue; // Skip main display connector or empty slots
        }

        // Each connector can have 2 small displays
        initializeSmallDisplay(connector, 0); // Left display
        initializeSmallDisplay(connector, 1); // Right display
        displayIndex += 2;
    }

    ESP_LOGI(TAG, "Display initialization completed. Main: %s, Small displays: %zu, CS write failures: %lu",
             mainDisplay_ ? "Yes" : "No", smallDisplays_.size(),
             static_cast<unsigned long>(getCsWriteFailureCount()));

    return true;
}

void DisplayManager::initChipSelectShadow() {
    CsLock lock(csMutex_);

    // 0xFF = every CS deselected; the MCP ignores OLAT bits of input pins.
    olatShadow_.assign(mcps_.size(), 0xFF);

    for (size_t connector = 0; connector < mcps_.size(); ++connector) {
        if (!mcps_[connector]) continue;
        Adafruit_I2CDevice* dev = McpI2cAccess::device(*mcps_[connector]);
        if (!dev) continue;

        // Start from the real latch so non-CS output bits are preserved.
        Adafruit_BusIO_Register olat(dev, MCP23XXX_OLAT, 1);
        uint8_t value = 0;
        if (olat.read(&value)) {
            olatShadow_[connector] = value;
        } else {
            ESP_LOGW(TAG, "Could not read OLAT of MCP on connector %u, assuming 0xFF",
                     static_cast<unsigned>(connector));
        }
    }
}

bool DisplayManager::writeOlatLocked(const size_t connector, const uint8_t value) const {
    Adafruit_I2CDevice* dev = (connector < mcps_.size() && mcps_[connector])
        ? McpI2cAccess::device(*mcps_[connector]) : nullptr;

    if (dev) {
        // One I2C transaction: [addr][OLAT][value], STOP.
        Adafruit_BusIO_Register olat(dev, MCP23XXX_OLAT, 1);
        for (int attempt = 0; attempt <= kCsWriteRetries; ++attempt) {
            if (attempt > 0) {
                csWriteRetries_.fetch_add(1, std::memory_order_relaxed);
                vTaskDelay(pdMS_TO_TICKS(kCsWriteRetryDelayMs));
            }
            if (olat.write(value)) {
                // Updated only on an ACKed write. Every call rewrites all CS
                // bits, so a stale shadow cannot keep a panel selected.
                olatShadow_[connector] = value;
                return true;
            }
        }
    }

    const uint32_t failures = csWriteFailures_.fetch_add(1, std::memory_order_relaxed) + 1;
    if (failures == 1 || failures % kCsFailureLogEvery == 0) {
        ESP_LOGW(TAG, "CS write to MCP on connector %u failed after %d attempts (value 0x%02X); "
                      "%lu failures, %lu retries so far",
                 static_cast<unsigned>(connector), kCsWriteRetries + 1, value,
                 static_cast<unsigned long>(failures),
                 static_cast<unsigned long>(getCsWriteRetryCount()));
    }
    return false;
}

bool DisplayManager::applyChipSelect(const int selectConnector, const uint8_t selectMask) const {
    CsLock lock(csMutex_);

    if (olatShadow_.size() != mcps_.size()) {
        olatShadow_.resize(mcps_.size(), 0xFF);
    }

    // Deselect every other MCP first, then select the target, so two panels
    // are never selected together.
    bool othersDeselected = true;
    for (size_t connector = 0; connector < mcps_.size(); ++connector) {
        if (!mcps_[connector] || static_cast<int>(connector) == selectConnector) continue;
        if (!writeOlatLocked(connector, olatShadow_[connector] | kAllCsMask)) {
            othersDeselected = false;
        }
    }

    if (selectConnector < 0) {
        return othersDeselected;
    }
    if (selectConnector >= static_cast<int>(mcps_.size()) || !mcps_[selectConnector]) {
        return false;
    }

    const auto target = static_cast<size_t>(selectConnector);
    const auto deselected = static_cast<uint8_t>(olatShadow_[target] | kAllCsMask);
    if (!othersDeselected) {
        // Another panel may still be selected: keep this MCP deselected too
        // and report failure, so the caller sends no SPI data.
        writeOlatLocked(target, deselected);
        return false;
    }
    return writeOlatLocked(target, static_cast<uint8_t>(deselected & ~selectMask));
}

void DisplayManager::initializeMainDisplay(const int connector) {
    ESP_LOGI(TAG, "Initializing main display on connector %d", connector);

    // CS pin is controlled by MCP, use proper backlight pin
    int virtualCs = -1; // We'll control CS manually through MCP
    mainDisplay_ = std::make_unique<LGFX_ESP32_SPI>(virtualCs, TFT_DC, BCKL320); // Use BCKL320 pin

    // Select main display before initialization
    deselectAllDisplays();
    vTaskDelay(pdMS_TO_TICKS(10));
    if (!selectMainDisplay()) {
        ESP_LOGW(TAG, "Main display CS select failed; init commands may not reach the panel");
    }

    // Stock init list at kMainPanelInitSpiHz, then kMainPanelRunSpiHz.
    // Note: LGFX init() always returns true, and there is no MISO to read
    // the panel back, so a failed init cannot be detected here.
    if (mainDisplay_->initWithClockRamp()) {
        mainDisplay_->setBrightness(128);
        mainDisplay_->fillScreen(0x0000); // Black
        ESP_LOGI(TAG, "Main display initialized successfully");
    } else {
        ESP_LOGE(TAG, "Failed to initialize main display");
        mainDisplay_.reset();
    }

    // Deselect after initialization
    deselectAllDisplays();
}

void DisplayManager::initializeSmallDisplay(const int connector, const int position) {
    ESP_LOGI(TAG, "Initializing small display on connector %d, position %d", connector, position);

    // CS pin is controlled by MCP (left or right)
    int virtualCs = -1; // We'll control CS manually through MCP
    auto smallDisplay = std::make_unique<LGFX_ESP32_SPI_Small>(virtualCs, TFT_DC);

    // Select the display before initialization
    if (!selectSmallDisplay(connector, position)) {
        ESP_LOGW(TAG, "Small display CS select failed (connector %d, position %d); init may not reach the panel",
                 connector, position);
    }

    auto cfg = smallDisplay->panel()->config();

    int rotation = (position % 2 ? 1 : 3);
    cfg.offset_rotation = rotation;

    if (rotation == 3) {
        cfg.offset_x = 24;
        cfg.offset_y = 2;
    } else {
        cfg.offset_x = 28;
    }
    smallDisplay->panel()->config(cfg);

    // Initialize the display
    if (smallDisplay->init()) {
        smallDisplay->setBrightness(64);
        smallDisplay->fillScreen((0x000000)); // Black

        SmallDisplayEntry entry;
        entry.display = std::move(smallDisplay);
        entry.connector = connector;
        entry.position = position;
        smallDisplays_.push_back(std::move(entry));

        ESP_LOGI(TAG, "Small display %zu initialized successfully", smallDisplays_.size() - 1);
    } else {
        ESP_LOGE(TAG, "Failed to initialize small display on connector %d, position %d", connector, position);
    }

    // Deselect after initialization
    deselectAllDisplays();
}

LGFX_ESP32_SPI_Small* DisplayManager::getSmallDisplay(const int index) const {
    if (index >= 0 && index < static_cast<int>(smallDisplays_.size())) {
        return smallDisplays_[index].display.get();
    }
    return nullptr;
}

bool DisplayManager::getSmallDisplayInfo(int index, int& connector, int& position) const {
    if (index >= 0 && index < static_cast<int>(smallDisplays_.size())) {
        connector = smallDisplays_[index].connector;
        position = smallDisplays_[index].position;
        return true;
    }
    return false;
}

int DisplayManager::getSmallDisplayIndexByPosition(const int buttonPosition) const {
    // Convert button position (1-based) to connector and side
    // Button position formula: position = (connector * 2) + 1 + side
    // Where side is 0 (left) or 1 (right)

    if (buttonPosition < 1) {
        return -1;
    }

    const int connector = (buttonPosition - 1) / 2;
    const int side = (buttonPosition - 1) % 2;

    // Search through small displays to find matching connector and position
    for (size_t i = 0; i < smallDisplays_.size(); ++i) {
        if (smallDisplays_[i].connector == connector && smallDisplays_[i].position == side) {
            return static_cast<int>(i);
        }
    }

    // No small display found for this button position (likely on main display connector)
    return -1;
}

bool DisplayManager::selectMainDisplay() const {
    if (mainDisplayConnector_ < 0 || mainDisplayConnector_ >= static_cast<int>(mcps_.size()) ||
        !mcps_[mainDisplayConnector_]) {
        deselectAllDisplays();
        return false;
    }
    // Main display uses MCP320_CS pin
    const bool ok = applyChipSelect(mainDisplayConnector_, static_cast<uint8_t>(1u << MCP320_CS));
    ESP_LOGD(TAG, "Selected main display on connector %d (%s)", mainDisplayConnector_, ok ? "ok" : "FAILED");
    return ok;
}

bool DisplayManager::selectSmallDisplay(const int connector, const int position) const {
    if (connector < 0 || connector >= static_cast<int>(mcps_.size()) || !mcps_[connector]) {
        deselectAllDisplays();
        return false;
    }
    const bool ok = applyChipSelect(connector, csMaskForPosition(position));
    ESP_LOGD(TAG, "Selected small display on connector %d, position %d (%s)",
             connector, position, ok ? "ok" : "FAILED");
    return ok;
}

bool DisplayManager::deselectAllDisplays() const {
    // One OLAT write per MCP (was three read-modify-writes per MCP).
    return applyChipSelect(-1, 0);
}

void DisplayManager::showHelloWorld() const {
    if (mainDisplay_) {
        ESP_LOGI(TAG, "Displaying test pattern on main display");

        selectMainDisplay();

        // Clear screen first
        mainDisplay_->fillScreen(0x0000); // Black

        // Fill with bright red to test basic pixel writing
        mainDisplay_->fillScreen(0xF800); // Bright red

        // Draw a green rectangle border to test drawing operations
        mainDisplay_->drawRect(10, 10, 300, 220, 0x07E0); // Green border

        // Fill center with blue
        mainDisplay_->fillRect(50, 50, 220, 140, 0x001F); // Blue center

        // White text on blue background
        mainDisplay_->setTextColor(0xFFFF, 0x001F); // White on blue
        mainDisplay_->setTextSize(4); // Larger text
        mainDisplay_->setCursor(60, 100);
        mainDisplay_->print("Hello");
        mainDisplay_->setCursor(60, 140);
        mainDisplay_->print("World!");

        deselectAllDisplays();

        ESP_LOGI(TAG, "Test pattern display completed");
    }
}

void DisplayManager::showRotationTestPattern() const {
    ESP_LOGI(TAG, "Displaying rotation test pattern on small displays");

    for (size_t i = 0; i < smallDisplays_.size(); ++i) {
        auto& entry = smallDisplays_[i];
        auto* display = entry.display.get();
        if (!display) continue;

        int connector = entry.connector;
        int position  = entry.position;

        ESP_LOGI(TAG, "Updating small display %zu (connector %d, pos %d)", i, connector, position);

        if (connector < 0 || connector >= mcps_.size() || !mcps_[connector]) {
            ESP_LOGE(TAG, "Invalid connector %d for small display %zu", connector, i);
            continue;
        }

        if (!selectSmallDisplay(connector, position)) {
            ESP_LOGW(TAG, "CS select failed for small display %zu, skipping", i);
            continue;
        }

        int w = 160, h = 80;
        display->fillScreen(0x0000);

        // Orientation bands
        display->fillRect(0, 0, w, 10, 0xF800);      // Top = Red
        display->fillRect(0, h - 10, w, 10, 0x001F); // Bottom = Blue
        display->fillRect(0, 0, 10, h, 0x07E0);      // Left = Green
        display->fillRect(w - 10, 0, 10, h, 0xFFE0); // Right = Yellow

        // Corner labels
        display->setTextColor(0xFFFF);
        display->setTextSize(1);
        display->setCursor(2, 2);      display->print("0");
        display->setCursor(w - 8, 2);  display->print("1");
        display->setCursor(2, h - 10); display->print("2");
        display->setCursor(w - 8, h - 10); display->print("3");

        // Center index
        display->setTextSize(2);
        display->setCursor(w / 2 - 12, h / 2 - 8);
        display->printf("%zu", i + 1);

        deselectAllDisplays();
    }
}

void DisplayManager::showDisplayNumbers() const {
    ESP_LOGI(TAG, "Displaying numbers on small displays");

    for (size_t i = 0; i < smallDisplays_.size(); ++i) {
        auto& entry = smallDisplays_[i];
        auto* display = entry.display.get();

        if (display) {
            // Calculate which connector and position this display is on
            int connector;
            if (mainDisplayConnector_ == 0) {
                connector = (static_cast<int>(i) / 2) + 1;
            } else {
                connector = static_cast<int>(i) / 2;
                if (connector >= mainDisplayConnector_) {
                    connector++; // Skip main display connector
                }
            }
            int position = static_cast<int>(i) % 2;

            selectSmallDisplay(connector, position);
            vTaskDelay(pdMS_TO_TICKS(10)); // Small delay for CS setup

            display->fillScreen(0x0000FF); // Black
            display->setTextColor(0xFFFF); // White
            display->setTextSize(2);
            display->setCursor(10, 30);
            display->printf("Disp\n %zu", i + 1);

            vTaskDelay(pdMS_TO_TICKS(10)); // Small delay before deselect
            deselectAllDisplays();

            ESP_LOGI(TAG, "Small display %zu content updated", i + 1);
        }
    }
}

void DisplayManager::setBacklight(const int displayIndex, const uint8_t brightness) const {
    if (displayIndex == 0 && mainDisplay_) {
        mainDisplay_->setBrightness(brightness);
    } else if (displayIndex > 0) {
        int smallIndex = displayIndex - 1;
        if (smallIndex < smallDisplays_.size()) {
            smallDisplays_[smallIndex].display->setBrightness(brightness);
        }
    }
}


// Global convenience functions
bool initializeDisplays(const std::vector<std::shared_ptr<Adafruit_MCP23X08>>& mcps, const int mainDisplayConnector) {
    if (!g_displayManager) {
        g_displayManager = std::make_unique<DisplayManager>();
    }

    return g_displayManager->initialize(mcps, mainDisplayConnector);
}

DisplayManager& getDisplayManager() {
    if (!g_displayManager) {
        g_displayManager = std::make_unique<DisplayManager>();
    }

    return *g_displayManager;
}

} // namespace FreeButton
