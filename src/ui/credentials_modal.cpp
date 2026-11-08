#include "credentials_modal.h"

#include "core/AuthLogic.h"

#include <WiFi.h>

#include <cstdio>
#include <cstring>
#include <esp_log.h>

static const char* TAG = "credentials_modal";

static lv_obj_t* modal = nullptr;
static lv_obj_t* hint_label = nullptr;

// 280 x 200 like the other modals: QR (140 px, quiet zone on) on the left,
// the text on the right, the hint along the bottom. The QR is a link,
// http://<ip>/#fbcred=<akid>:<secret> (<= 92 bytes for IPv4: version 6, still
// 3 px modules): a phone camera opens the IDE, which stores the credentials
// from the fragment (browsers never send it) and offers copy / save. Without
// an IP address the plain FBCRED:<akid>:<secret> text is encoded.
void show_credentials_modal(lv_obj_t* screen, const char* access_key_id, const char* secret, const char* hint) {
    if (!screen || !access_key_id || !secret) return;
    hide_credentials_modal();

    modal = lv_obj_create(screen);
    if (!modal) return;
    lv_obj_move_foreground(modal);
    lv_obj_set_size(modal, 280, 200);
    lv_obj_center(modal);
    lv_obj_set_style_bg_opa(modal, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(modal, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(modal, 2, 0);
    lv_obj_set_style_border_color(modal, lv_color_hex(0x666666), 0);
    lv_obj_set_style_radius(modal, 8, 0);
    lv_obj_set_style_pad_all(modal, 6, 0);
    lv_obj_set_scrollbar_mode(modal, LV_SCROLLBAR_MODE_OFF);

    char payload[fbauth::kQrUrlMax + 1];
    IPAddress ip = WiFi.localIP();
    if (ip == IPAddress(0, 0, 0, 0)) ip = WiFi.softAPIP();  // setup AP (captive portal)
    char host[16];
    snprintf(host, sizeof host, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    const bool haveQr = (ip != IPAddress(0, 0, 0, 0) && fbauth::formatQrUrl(host, access_key_id, secret, payload, sizeof payload)) ||
                        fbauth::formatQrPayload(access_key_id, secret, payload, sizeof payload);
    lv_obj_t* qr = haveQr ? lv_qrcode_create(modal) : nullptr;
    if (qr) {
        lv_qrcode_set_size(qr, 140);
        lv_qrcode_set_dark_color(qr, lv_color_black());
        lv_qrcode_set_light_color(qr, lv_color_white());
        lv_qrcode_set_quiet_zone(qr, true);
        if (lv_qrcode_update(qr, payload, strlen(payload)) != LV_RESULT_OK) {
            ESP_LOGE(TAG, "QR code generation failed");
            lv_obj_delete(qr);
            qr = nullptr;
        } else {
            lv_obj_align(qr, LV_ALIGN_TOP_LEFT, 0, 0);
        }
    }
    memset(payload, 0, sizeof payload);

    // Key id and secret in groups of 10 (proportional font: the QR code is
    // the reliable way to transfer them; the text is for checking).
    char text[160];
    snprintf(text, sizeof text, "Key ID\n%.10s\n%.10s\nSecret\n%.10s\n%.10s\n%.10s\n%.10s", access_key_id,
             access_key_id + 10, secret, secret + 10, secret + 20, secret + 30);
    lv_obj_t* label = lv_label_create(modal);
    lv_label_set_text(label, text);
    memset(text, 0, sizeof text);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 148, 0);

    hint_label = lv_label_create(modal);
    lv_label_set_long_mode(hint_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint_label, 264);
    lv_label_set_text(hint_label, hint ? hint : "");
    lv_obj_set_style_text_color(hint_label, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(hint_label, &lv_font_montserrat_12, 0);
    lv_obj_align(hint_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    ESP_LOGI(TAG, "credentials shown");
}

void set_credentials_modal_hint(const char* hint) {
    if (hint_label) lv_label_set_text(hint_label, hint ? hint : "");
}

void hide_credentials_modal(void) {
    if (!modal) return;
    lv_obj_delete(modal);  // frees the QR canvas and the labels
    modal = nullptr;
    hint_label = nullptr;
    ESP_LOGI(TAG, "credentials hidden");
}

bool is_credentials_modal_visible(void) { return modal != nullptr; }
