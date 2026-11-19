// Emulator shim: ESP_LOGx goes to the host (the emulator's 'log' event), as
// the firmware's goes to the UART. Debug level is compiled out, as on the
// device (CONFIG_LOG_DEFAULT_LEVEL_INFO).
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
void fbe_log(int level, const char* tag, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
#ifdef __cplusplus
}
#endif
#define ESP_LOGE(tag, fmt, ...) fbe_log('E', tag, fmt, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) fbe_log('W', tag, fmt, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) fbe_log('I', tag, fmt, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) do { (void)(tag); } while (0)
#define ESP_LOGV(tag, fmt, ...) do { (void)(tag); } while (0)
