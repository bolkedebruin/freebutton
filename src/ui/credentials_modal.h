#pragma once

// On-device screen with the API credentials (SigV4 access key id + secret)
// as text and as a QR code "FBCRED:<akid>:<secret>". Shown from the factory
// button flow (DeviceInfoManager.cpp). All functions: LVGL task / LVGL lock
// held. The QR canvas (~2.5 KB of LVGL pool) exists only while the screen is
// shown.

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

void show_credentials_modal(lv_obj_t* screen, const char* access_key_id, const char* secret, const char* hint);
void set_credentials_modal_hint(const char* hint);
void hide_credentials_modal(void);
bool is_credentials_modal_visible(void);

#ifdef __cplusplus
}
#endif
