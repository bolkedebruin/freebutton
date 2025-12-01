#include "NtpClient.h"

#include <Arduino.h>
#include <esp_log.h>
#include <esp_sntp.h>
#include <time.h>

namespace FreeButton {

bool NtpClient::initialized = false;

void NtpClient::initialize(const std::string& ntpServer, const std::string& timezone) {
    ESP_LOGI(TAG, "Initializing NTP client with server: %s, timezone: %s",
             ntpServer.c_str(), timezone.c_str());

    const char* server = ntpServer.empty() ? DEFAULT_NTP_SERVER : ntpServer.c_str();
    const char* tz = timezone.empty() ? DEFAULT_TIMEZONE : timezone.c_str();

    if (initialized) {
        ESP_LOGW(TAG, "NTP already initialized, reinitializing...");
        esp_sntp_stop();
        initialized = false;
    }

    configTzTime(tz, server);

    esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
    esp_sntp_init();

    initialized = true;
    ESP_LOGI(TAG, "NTP client initialized successfully");
}

bool NtpClient::syncTime() {
    if (!initialized) {
        ESP_LOGE(TAG, "NTP not initialized");
        return false;
    }

    ESP_LOGI(TAG, "Requesting time synchronization...");

    time_t now = 0;
    struct tm timeinfo = { 0 };
    int retry = 0;
    const int retry_count = 15;

    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
        ESP_LOGD(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    time(&now);
    localtime_r(&now, &timeinfo);

    if (timeinfo.tm_year < (2016 - 1900)) {
        ESP_LOGE(TAG, "Time synchronization failed");
        return false;
    }

    ESP_LOGI(TAG, "Time synchronized: %04d-%02d-%02d %02d:%02d:%02d",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

    return true;
}

bool NtpClient::isTimeSynchronized() {
    if (!initialized) {
        return false;
    }

    time_t now;
    struct tm timeinfo = { 0 };
    time(&now);
    localtime_r(&now, &timeinfo);

    return timeinfo.tm_year >= (2016 - 1900);
}

} // namespace FreeButton