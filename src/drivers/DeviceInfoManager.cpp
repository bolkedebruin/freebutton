#include "scripting/WebSocketREPL.h"
#include "DeviceInfoManager.h"
#include "DeviceInfo.h"
#include "LvglDisplay.h"
#include "../ui/device_info_modal.h"
#include "../ui/credentials_modal.h"
#include "../core/Auth.h"
#include <Arduino.h>
#include <esp_log.h>
#include <memory>
#include <lvgl.h>

namespace FreeButton {

static auto TAG = "DeviceInfo";

// Factory button gestures:
//   hold 2 s            device info, while held (release closes it)
//   keep holding to 6 s the info turns into the API credentials screen
//                       (key id, secret, QR "FBCRED:<akid>:<secret>"); it
//                       stays after release
//   on that screen:
//     short press       close (also closes by itself after 2 minutes)
//     new hold >= 10 s  rotate: new credentials are generated, stored and
//                       shown; the old ones stop working at once. Released
//                       earlier: nothing changes.
// The rotation hold must start on the credentials screen, so one long hold
// from idle can never rotate.
static constexpr uint32_t CREDENTIALS_AFTER_MS = 6000;
static constexpr uint32_t ROTATE_AFTER_MS = 10000;
static constexpr uint32_t CREDENTIALS_TIMEOUT_MS = 120000;

enum class Phase : uint8_t {
    Idle,
    InfoHeld,         // info shown, button held
    CredentialsHeld,  // switched to credentials, button still held
    Credentials,      // credentials shown, button released
    RotateHeld,       // new hold on the credentials screen
    RotatedHeld,      // rotated during this hold
};

enum class Screen : uint8_t { None, Info, Credentials };

// All state below is shared between the factory-button task and
// modalCheckTimerCallback (LVGL task) and only touched under the LVGL lock.
static Phase phase = Phase::Idle;
static Screen wanted = Screen::None;   // what the button asks for
static Screen shown = Screen::None;    // what is on screen
static bool rotateRequested = false;
static bool hintDirty = false;
static char hint[64] = "";
static uint32_t credentialsSince = 0;
static std::string* pendingDeviceInfoText = nullptr;
static lv_timer_t* modalCheckTimer = nullptr;

static constexpr const char* HINT_IDLE = "Scan to open the IDE. Press: close. Hold 10 s: new keys";

static void setHint(const char* text) {
    snprintf(hint, sizeof hint, "%s", text);
    hintDirty = true;
}

static void showCredentials(lv_obj_t* screen) {
    char akid[fbauth::kAccessKeyIdLen + 1];
    char secret[fbauth::kSecretLen + 1];
    fbauth::copyCredentials(akid, secret);
    show_credentials_modal(screen, akid, secret, hint);
    memset(secret, 0, sizeof secret);
    hintDirty = false;
}

// LVGL task, lock held: bring the screen in line with what was asked.
static void modalCheckTimerCallback(lv_timer_t* timer) {
    (void)timer;
    auto* screen = lv_scr_act();

    if (rotateRequested) {
        rotateRequested = false;
        // NVS write (a few ms) on the LVGL task: the factory-button task's
        // stack is too small for it, and this is a rare, deliberate action.
        const bool rotated = fbauth::rotateCredentials();
        if (rotated) Scripting::WebSocketREPL::getInstance().requestCloseAll();
        setHint(rotated ? "New keys. Old keys no longer work." : "Rotation failed (NVS)");
        if (shown == Screen::Credentials && screen) {
            showCredentials(screen);  // rebuild with the new pair
        }
    }

    if (wanted != shown) {
        if (shown == Screen::Info) hide_device_info_modal();
        if (shown == Screen::Credentials) hide_credentials_modal();
        shown = Screen::None;
        if (wanted == Screen::Info && pendingDeviceInfoText && screen) {
            show_device_info_modal(screen, pendingDeviceInfoText->c_str());
            shown = Screen::Info;
        } else if (wanted == Screen::Credentials && screen) {
            showCredentials(screen);
            shown = Screen::Credentials;
        } else {
            wanted = Screen::None;
        }
        if (shown != Screen::Info && pendingDeviceInfoText) {
            delete pendingDeviceInfoText;
            pendingDeviceInfoText = nullptr;
        }
    }

    if (shown == Screen::Credentials) {
        if (hintDirty) {
            set_credentials_modal_hint(hint);
            hintDirty = false;
        }
        if (phase == Phase::Credentials && millis() - credentialsSince >= CREDENTIALS_TIMEOUT_MS) {
            phase = Phase::Idle;
            wanted = Screen::None;
        }
    }
}

// Create timer lazily when needed. Caller must hold the LVGL lock.
static void ensureTimerExists() {
    if (!modalCheckTimer) {
        modalCheckTimer = lv_timer_create(modalCheckTimerCallback, 50, nullptr);
        ESP_LOGI(TAG, "Created modal check timer");
    }
}

static void requestInfo() {
    // DeviceInfoCollector is stateless (sizeof == 1); collectInfo() only
    // reads the LVGL memory snapshot.
    DeviceInfoCollector collector;
    DeviceInfo info = collector.collectInfo();
    std::string text = DeviceInfoCollector::formatDeviceInfo(info);
    delete pendingDeviceInfoText;
    pendingDeviceInfoText = new std::string(std::move(text));
    wanted = Screen::Info;
}

// Runs in the factory-button task, not the LVGL task. Every LVGL call here,
// and the state shared with modalCheckTimerCallback (which runs in the LVGL
// task with the lock held), is accessed under the LVGL lock.
void onDeviceInfoButtonEvent(ButtonEvent event) {
    ESP_LOGD(TAG, "Factory button event: %d", static_cast<int>(event));
    const uint32_t held = g_factoryButton.getPressedDuration();

    auto& lvglDisplay = getLvglDisplay();
    lvglDisplay.lock();
    ensureTimerExists();

    switch (event) {
        case ButtonEvent::LONG_PRESS_START:
            if (phase == Phase::Idle) {
                ESP_LOGI(TAG, "Long press - showing device info");
                requestInfo();
                phase = Phase::InfoHeld;
            } else if (phase == Phase::Credentials) {
                phase = Phase::RotateHeld;
                setHint("Keep holding for new keys...");
            }
            break;

        case ButtonEvent::HOLD:
            if (phase == Phase::InfoHeld && held >= CREDENTIALS_AFTER_MS) {
                ESP_LOGI(TAG, "Showing API credentials");
                setHint(HINT_IDLE);
                wanted = Screen::Credentials;
                phase = Phase::CredentialsHeld;
            } else if (phase == Phase::RotateHeld) {
                if (held >= ROTATE_AFTER_MS) {
                    ESP_LOGW(TAG, "Rotating API credentials");
                    rotateRequested = true;
                    phase = Phase::RotatedHeld;
                } else {
                    char text[48];
                    snprintf(text, sizeof text, "Keep holding: new keys in %u s",
                             static_cast<unsigned>((ROTATE_AFTER_MS - held + 999) / 1000));
                    setHint(text);
                }
            }
            break;

        case ButtonEvent::LONG_PRESS_END:
            switch (phase) {
                case Phase::InfoHeld:
                    ESP_LOGI(TAG, "Long press ended - hiding device info");
                    wanted = Screen::None;
                    phase = Phase::Idle;
                    break;
                case Phase::RotateHeld:
                    setHint(HINT_IDLE);
                    [[fallthrough]];
                case Phase::CredentialsHeld:
                case Phase::RotatedHeld:
                    phase = Phase::Credentials;
                    credentialsSince = millis();
                    break;
                default:
                    break;
            }
            break;

        case ButtonEvent::SHORT_PRESS:
            if (phase == Phase::Credentials) {
                wanted = Screen::None;
                phase = Phase::Idle;
            }
            break;

        default:
            break;
    }

    lvglDisplay.unlock();
}

bool initializeDeviceInfoButton(int factoryButtonPin) {
    ESP_LOGI(TAG, "Initializing device info button on pin %d", factoryButtonPin);

    // Initialize factory button
    if (!g_factoryButton.initialize(factoryButtonPin)) {
        ESP_LOGE(TAG, "Failed to initialize factory button");
        return false;
    }

    // Set button callback
    g_factoryButton.setCallback(onDeviceInfoButtonEvent);

    // Note: LVGL timer will be created on first use (after LVGL is initialized)

    ESP_LOGI(TAG, "Device info button initialized successfully");
    return true;
}

void startDeviceInfoButton() {
    ESP_LOGI(TAG, "Starting device info button");
    g_factoryButton.start();
}

void stopDeviceInfoButton() {
    ESP_LOGI(TAG, "Stopping device info button");
    g_factoryButton.stop();

    // Must not be called from the LVGL task (the LVGL mutex is not recursive).
    auto& lvglDisplay = getLvglDisplay();
    lvglDisplay.lock();
    if (shown == Screen::Info) hide_device_info_modal();
    if (shown == Screen::Credentials) hide_credentials_modal();
    shown = wanted = Screen::None;
    phase = Phase::Idle;
    lvglDisplay.unlock();
}

} // namespace FreeButton
