#include "DisplayManager.h"
#include "pins.h"
#include <esp_log.h>

namespace ButtonPlus {

static const char* TAG = "DisplayManager";

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

void DisplayManager::initializeMainDisplay(std::shared_ptr<Adafruit_MCP23008> mcp, int connector) {
    ESP_LOGI(TAG, "Initializing main display on connector %d", connector);

    // CS pin is controlled by MCP, use proper backlight pin
    int virtualCs = -1; // We'll control CS manually through MCP
    mainDisplay_ = std::make_unique<LGFX_ESP32_SPI>(virtualCs, TFT_DC, BCKL320); // Use BCKL320 pin

    // Select main display before initialization
    deselectAllDisplays();
    vTaskDelay(pdMS_TO_TICKS(10));
    mcp->digitalWrite(MCP320_CS, LOW);  // Select main display
    vTaskDelay(pdMS_TO_TICKS(10)); // CS setup time

    // Initialize the display with proper timing
    if (mainDisplay_->init()) {
        vTaskDelay(pdMS_TO_TICKS(120)); // Match TFT_eSPI delay after init
        mainDisplay_->setRotation(1); // Landscape
        mainDisplay_->setSwapBytes(true); // Important for proper color display
        mainDisplay_->setBrightness(128);
        vTaskDelay(pdMS_TO_TICKS(50)); // Allow brightness to settle
        mainDisplay_->fillScreen(0x0000); // Black
        ESP_LOGI(TAG, "Main display initialized successfully");
    } else {
        ESP_LOGE(TAG, "Failed to initialize main display");
        mainDisplay_.reset();
    }

    // Deselect after initialization
    mcp->digitalWrite(MCP320_CS, HIGH);
    vTaskDelay(pdMS_TO_TICKS(10));
}

void DisplayManager::initializeSmallDisplay(std::shared_ptr<Adafruit_MCP23008> mcp, int connector, int position) {
    ESP_LOGI(TAG, "Initializing small display on connector %d, position %d", connector, position);

    // CS pin is controlled by MCP (left or right)
    int virtualCs = -1; // We'll control CS manually through MCP
    auto smallDisplay = std::make_unique<LGFX_ESP32_SPI_Small>(virtualCs, TFT_DC);

    // Select the display before initialization
    deselectAllDisplays();
    vTaskDelay(pdMS_TO_TICKS(10));
    int csPin = (position == 0) ? MCP160_CS_LEFT : MCP160_CS_RIGHT;
    mcp->digitalWrite(csPin, LOW);
    vTaskDelay(pdMS_TO_TICKS(10)); // CS setup time

    // Initialize the display
    if (smallDisplay->init()) {
        vTaskDelay(pdMS_TO_TICKS(50)); // Match initialization timing
        // Set rotation based on position like in the working implementation
        smallDisplay->setRotation(position % 2 ? 3 : 1);
        smallDisplay->setSwapBytes(true); // Important for proper color display
        smallDisplay->setBrightness(128);
        smallDisplay->fillScreen(0x0000); // Black
        smallDisplays_.push_back(std::move(smallDisplay));
        ESP_LOGI(TAG, "Small display %zu initialized successfully", smallDisplays_.size() - 1);
    } else {
        ESP_LOGE(TAG, "Failed to initialize small display on connector %d, position %d", connector, position);
    }

    // Deselect after initialization
    mcp->digitalWrite(csPin, HIGH);
    vTaskDelay(pdMS_TO_TICKS(10));
}

LGFX_ESP32_SPI_Small* DisplayManager::getSmallDisplay(int index) {
    if (index >= 0 && index < smallDisplays_.size()) {
        return smallDisplays_[index].get();
    }
    return nullptr;
}

void DisplayManager::selectMainDisplay() {
    deselectAllDisplays();
    if (mainDisplayConnector_ >= 0 && mainDisplayConnector_ < mcps_.size() && mcps_[mainDisplayConnector_]) {
        // Main display uses display index connector * 2 (like TFT_eSPI implementation)
        int displayIndex = mainDisplayConnector_ * 2;
        // Use left CS for display index (even numbers use left CS)
        mcps_[mainDisplayConnector_]->digitalWrite(MCP160_CS_LEFT, LOW);
        ESP_LOGD(TAG, "Selected main display on connector %d (display index %d)", mainDisplayConnector_, displayIndex);
    }
}

void DisplayManager::selectSmallDisplay(int connector, int position) {
    deselectAllDisplays();
    if (connector >= 0 && connector < mcps_.size() && mcps_[connector]) {
        int csPin = (position == 0) ? MCP160_CS_LEFT : MCP160_CS_RIGHT;
        mcps_[connector]->digitalWrite(csPin, LOW);
        ESP_LOGD(TAG, "Selected small display on connector %d, position %d", connector, position);
    }
}

void DisplayManager::deselectAllDisplays() {
    for (size_t i = 0; i < mcps_.size(); ++i) {
        if (mcps_[i]) {
            mcps_[i]->digitalWrite(MCP160_CS_LEFT, HIGH);
            mcps_[i]->digitalWrite(MCP160_CS_RIGHT, HIGH);
        }
    }
}

void DisplayManager::showHelloWorld() {
    if (mainDisplay_) {
        ESP_LOGI(TAG, "Displaying test pattern on main display");

        selectMainDisplay();
        vTaskDelay(pdMS_TO_TICKS(100)); // Longer delay for CS setup

        // Clear screen first
        mainDisplay_->fillScreen(0x0000); // Black
        vTaskDelay(pdMS_TO_TICKS(50));

        // Fill with bright red to test basic pixel writing
        mainDisplay_->fillScreen(0xF800); // Bright red
        vTaskDelay(pdMS_TO_TICKS(200));

        // Draw a green rectangle border to test drawing operations
        mainDisplay_->drawRect(10, 10, 300, 220, 0x07E0); // Green border
        vTaskDelay(pdMS_TO_TICKS(50));

        // Fill center with blue
        mainDisplay_->fillRect(50, 50, 220, 140, 0x001F); // Blue center
        vTaskDelay(pdMS_TO_TICKS(50));

        // White text on blue background
        mainDisplay_->setTextColor(0xFFFF, 0x001F); // White on blue
        mainDisplay_->setTextSize(4); // Larger text
        mainDisplay_->setCursor(60, 100);
        mainDisplay_->print("HELLO");
        mainDisplay_->setCursor(60, 140);
        mainDisplay_->print("WORLD");

        vTaskDelay(pdMS_TO_TICKS(100));
        deselectAllDisplays();

        ESP_LOGI(TAG, "Test pattern display completed");
    }
}

void DisplayManager::showDisplayNumbers() {
    ESP_LOGI(TAG, "Displaying numbers on small displays");

    for (size_t i = 0; i < smallDisplays_.size(); ++i) {
        auto* display = smallDisplays_[i].get();
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

            display->fillScreen(0x0000); // Black
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

void DisplayManager::setBacklight(int displayIndex, uint8_t brightness) {
    if (displayIndex == 0 && mainDisplay_) {
        mainDisplay_->setBrightness(brightness);
    } else if (displayIndex > 0) {
        int smallIndex = displayIndex - 1;
        if (smallIndex < smallDisplays_.size()) {
            smallDisplays_[smallIndex]->setBrightness(brightness);
        }
    }
}

// Global convenience functions
bool initializeDisplays(const std::vector<std::shared_ptr<Adafruit_MCP23008>>& mcps, int mainDisplayConnector) {
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