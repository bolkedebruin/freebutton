//
// Created by Claude Code on 24/12/2025.
// OTA Manager for remote firmware updates
//

#ifndef OTAMANAGER_H
#define OTAMANAGER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief OTA error codes (no exceptions)
 */
enum class OtaResult {
    SUCCESS = 0,
    FAILED_MQTT_STOP = 1,
    FAILED_HTTP_CLIENT = 2,
    FAILED_OTA_DOWNLOAD = 3,
    FAILED_TASK_CREATE = 4
};

/**
 * @brief Start remote OTA update from API
 * @param firmware_id Optional firmware ID (0 for latest)
 * @return OtaResult error code
 */
OtaResult ota_start_remote_update(int firmware_id);

/**
 * @brief Check if OTA is in progress
 */
bool ota_is_in_progress(void);

#ifdef __cplusplus
}
#endif

#endif // OTAMANAGER_H
