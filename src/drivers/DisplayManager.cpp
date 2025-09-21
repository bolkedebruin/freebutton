#include "DisplayManager.h"
#include "pins.h"
#include <esp_log.h>

namespace ButtonPlus {

static auto TAG = "DisplayManager";

// Global instance
std::unique_ptr<DisplayManager> g_displayManager;

bool DisplayManager::initialize(const std::vector<std::shared_ptr<Adafruit_MCP23008>>& mcps, int mainDisplayConnector) {
    ESP_LOGI(TAG, "Initializing displays...");

    mcps_ = mcps;
    mainDisplayConnector_ = mainDisplayConnector;

    // Initialize main display if connector specified
    if (mainDisplayConnector >= 0 && mainDisplayConnector < mcps_.size() && mcps_[mainDisplayConnector]) {
        initializeMainDisplay(mcps_[mainDisplayConnector], mainDisplayConnector);
    }

    // Initialize small displays on remaining connectors
    int displayIndex = 0;
    for (int connector = 0; connector < mcps_.size(); ++connector) {
        if (connector == mainDisplayConnector || !mcps_[connector]) {
            continue; // Skip main display connector or empty slots
        }

        // Each connector can have 2 small displays
        initializeSmallDisplay(mcps_[connector], connector, 0); // Left display
        initializeSmallDisplay(mcps_[connector], connector, 1); // Right display
        displayIndex += 2;
    }

    ESP_LOGI(TAG, "Display initialization completed. Main: %s, Small displays: %zu",
             mainDisplay_ ? "Yes" : "No", smallDisplays_.size());

    return true;
}

void DisplayManager::initializeMainDisplay(const std::shared_ptr<Adafruit_MCP23008>& mcp, int connector) {
    ESP_LOGI(TAG, "Initializing main display on connector %d", connector);

    // CS pin is controlled by MCP, use proper backlight pin
    int virtualCs = -1; // We'll control CS manually through MCP
    mainDisplay_ = std::make_unique<LGFX_ESP32_SPI>(virtualCs, TFT_DC, BCKL320); // Use BCKL320 pin

    // Select main display before initialization
    deselectAllDisplays();
    vTaskDelay(pdMS_TO_TICKS(10));
    mcp->digitalWrite(MCP320_CS, LOW);  // Select main display

    // Initialize the display with proper timing
    if (mainDisplay_->init()) {
        mainDisplay_->setBrightness(128);
        mainDisplay_->fillScreen(0x0000); // Black
        ESP_LOGI(TAG, "Main display initialized successfully");
    } else {
        ESP_LOGE(TAG, "Failed to initialize main display");
        mainDisplay_.reset();
    }

    // Deselect after initialization
    mcp->digitalWrite(MCP320_CS, HIGH);
}

void DisplayManager::initializeSmallDisplay(const std::shared_ptr<Adafruit_MCP23008>& mcp, int connector, int position) {
    ESP_LOGI(TAG, "Initializing small display on connector %d, position %d", connector, position);

    // CS pin is controlled by MCP (left or right)
    int virtualCs = -1; // We'll control CS manually through MCP
    auto smallDisplay = std::make_unique<LGFX_ESP32_SPI_Small>(virtualCs, TFT_DC);

    // Select the display before initialization
    deselectAllDisplays();
    int csPin = (position == 0) ? MCP160_CS_LEFT : MCP160_CS_RIGHT;
    mcp->digitalWrite(csPin, LOW);

    auto cfg = smallDisplay->panel()->config();

    int rotation = (position % 2 ? 1 : 3);
    cfg.offset_rotation = rotation;

    if (rotation == 3) {
        cfg.offset_x = 24;
    } else {
        cfg.offset_x = 28;
    }
    smallDisplay->panel()->config(cfg);

    // Initialize the display
    if (smallDisplay->init()) {
        //smallDisplay->setSwapBytes(true); // Important for proper color display
        smallDisplay->setBrightness(128);
        smallDisplay->fillScreen((0x0000FF)); // Black

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
    mcp->digitalWrite(csPin, HIGH);
}

LGFX_ESP32_SPI_Small* DisplayManager::getSmallDisplay(const int index) const {
    if (index >= 0 && index < static_cast<int>(smallDisplays_.size())) {
        return smallDisplays_[index].display.get();
    }
    return nullptr;
}

void DisplayManager::selectMainDisplay() const {
    deselectAllDisplays();
    if (mainDisplayConnector_ >= 0 && mainDisplayConnector_ < mcps_.size() && mcps_[mainDisplayConnector_]) {
        // Main display uses display index connector * 2 (because each connector can have 2 small displays)
        int displayIndex = mainDisplayConnector_ * 2;
        mcps_[mainDisplayConnector_]->digitalWrite(MCP160_CS_LEFT, LOW);
        ESP_LOGD(TAG, "Selected main display on connector %d (display index %d)", mainDisplayConnector_, displayIndex);
    }
}

void DisplayManager::selectSmallDisplay(const int connector, const int position) const {
    deselectAllDisplays();
    if (connector >= 0 && connector < mcps_.size() && mcps_[connector]) {
        const int csPin = (position == 0) ? MCP160_CS_LEFT : MCP160_CS_RIGHT;
        mcps_[connector]->digitalWrite(csPin, LOW);
        ESP_LOGD(TAG, "Selected small display on connector %d, position %d", connector, position);
    }
}

void DisplayManager::deselectAllDisplays() const {
    for (const auto & mcp : mcps_) {
        if (mcp) {
            mcp->digitalWrite(MCP160_CS_LEFT, HIGH);
            mcp->digitalWrite(MCP160_CS_RIGHT, HIGH);
        }
    }
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
        mainDisplay_->print("HELLO");
        mainDisplay_->setCursor(60, 140);
        mainDisplay_->print("Bolke!");

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
        int csPin     = (position == 0) ? MCP160_CS_LEFT : MCP160_CS_RIGHT;

        ESP_LOGI(TAG, "Updating small display %zu (connector %d, pos %d)", i, connector, position);

        if (connector < 0 || connector >= mcps_.size() || !mcps_[connector]) {
            ESP_LOGE(TAG, "Invalid connector %d for small display %zu", connector, i);
            continue;
        }

        deselectAllDisplays();
        mcps_[connector]->digitalWrite(csPin, LOW);

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

        mcps_[connector]->digitalWrite(csPin, HIGH);
    }
}

void DisplayManager::showDisplayNumbers() const {
    ESP_LOGI(TAG, "Displaying numbers on small displays");

    for (size_t i = 0; i < smallDisplays_.size(); ++i) {
        auto& entry = smallDisplays_[i];
        auto* display = entry.display.get();

        if (display) {
            // Calculate which connector and position this display is on
            int connector = (static_cast<int>(i) / 2) + 1; // Skip connector 0 (main display)
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
bool initializeDisplays(const std::vector<std::shared_ptr<Adafruit_MCP23008>>& mcps, const int mainDisplayConnector) {
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

} // namespace ButtonPlus