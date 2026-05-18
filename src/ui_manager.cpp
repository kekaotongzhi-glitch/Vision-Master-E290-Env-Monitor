#include "ui_manager.h"
#include "lvgl_eink_bridge.h"
#include <lvgl.h>
#include <math.h>
#include <qrcode.h>

static lv_obj_t *label_temp = nullptr;
static lv_obj_t *label_hum = nullptr;
static lv_obj_t *label_batt_pct = nullptr;
static lv_obj_t *label_charge = nullptr;
static lv_obj_t *label_wifi = nullptr;
static lv_obj_t *label_mqtt = nullptr;
static lv_obj_t *label_ble = nullptr;
static lv_obj_t *label_time = nullptr;
static lv_obj_t *label_env = nullptr;

static UiScreen current_screen = UiScreen::MAIN;
static RefreshType pending_refresh = RefreshType::NONE;
static String current_qr_payload;
static uint32_t last_full_refresh_ms = 0;
static uint32_t last_status_refresh_ms = 0;
static uint32_t status_refresh_requested_ms = 0;
static uint8_t partial_refresh_count = 0;
static bool pending_status_refresh = false;

static WifiStatus current_wifi_status = WifiStatus::UNCONFIGURED;
static MqttStatus current_mqtt_status = MqttStatus::DISCONNECTED;
static bool provisioning_active = false;

static String last_temp_text;
static String last_hum_text;
static String last_batt_text;
static String last_status_text;
static String last_time_text;
static String last_env_text;
static float last_displayed_temp = NAN;
static float last_displayed_hum = NAN;
static int last_displayed_batt = -1;
static bool last_displayed_charging = false;
static int status_battery_percent = -1;
static bool status_charging = false;

static const uint32_t MAIN_FULL_REFRESH_MIN_MS = 30000;
static const uint32_t STATUS_REFRESH_DELAY_MS = 1000;
static const uint32_t STATUS_REFRESH_MIN_MS = 5000;
static const uint32_t GHOST_CLEANUP_MS = 20UL * 60UL * 1000UL;
static const uint8_t MAX_PARTIALS_BEFORE_FULL = 30;

static const char* wifi_status_text(WifiStatus status) {
    switch (status) {
        case WifiStatus::UNCONFIGURED: return "W CFG";
        case WifiStatus::CONNECTING: return "W ...";
        case WifiStatus::CONNECTED: return "W OK";
        case WifiStatus::OFFLINE: return "W OFF";
        default: return "W ?";
    }
}

static const char* mqtt_status_text(MqttStatus status) {
    switch (status) {
        case MqttStatus::CONNECTED: return "M OK";
        case MqttStatus::CONNECTING: return "M ...";
        case MqttStatus::DISCONNECTED: return "M OFF";
        default: return "M ?";
    }
}

static bool set_label_text_if_changed(lv_obj_t *label, String &cache, const String &text) {
    if (!label || cache == text) return false;
    cache = text;
    lv_label_set_text(label, text.c_str());
    return true;
}

static void update_status_line() {
    if (!label_wifi) return;

    String text = String(wifi_status_text(current_wifi_status)) + " " +
                  mqtt_status_text(current_mqtt_status) + " B";
    if (status_battery_percent < 0) {
        text += "--";
    } else {
        text += String(status_battery_percent);
    }
    if (status_charging) text += "+";
    if (provisioning_active) text += " BLE";

    set_label_text_if_changed(label_wifi, last_status_text, text);
}

static void layout_status_bar() {
    if (!label_batt_pct || !label_charge || !label_wifi || !label_mqtt || !label_ble) return;

    update_status_line();
    lv_obj_align(label_wifi, LV_ALIGN_TOP_RIGHT, -3, 3);
    lv_obj_add_flag(label_charge, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(label_batt_pct, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(label_mqtt, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(label_ble, LV_OBJ_FLAG_HIDDEN);
    if (label_env) {
        lv_obj_align(label_env, LV_ALIGN_BOTTOM_MID, 0, -2);
    }
}

void ui_request_refresh(RefreshType type, RefreshReason reason) {
    if (reason == RefreshReason::NETWORK_CHANGED && type == RefreshType::PARTIAL) {
        pending_status_refresh = true;
        status_refresh_requested_ms = millis();
        return;
    }
    if (type == RefreshType::FULL) {
        pending_refresh = RefreshType::FULL;
    } else if (type == RefreshType::PARTIAL && pending_refresh == RefreshType::NONE) {
        pending_refresh = RefreshType::PARTIAL;
    }
}

void ui_commit_refresh_if_needed() {
    uint32_t now = millis();
    if (pending_status_refresh &&
        (now - status_refresh_requested_ms) >= STATUS_REFRESH_DELAY_MS &&
        (now - last_status_refresh_ms) >= STATUS_REFRESH_MIN_MS) {
        pending_status_refresh = false;
        last_status_refresh_ms = now;
        if (pending_refresh == RefreshType::NONE) {
            pending_refresh = RefreshType::PARTIAL;
        }
    }

    if (pending_refresh == RefreshType::NONE) return;

    bool do_full = pending_refresh == RefreshType::FULL;
    if (!do_full && current_screen == UiScreen::MAIN) {
        if ((now - last_full_refresh_ms) >= GHOST_CLEANUP_MS || partial_refresh_count >= MAX_PARTIALS_BEFORE_FULL) {
            do_full = true;
        }
    }

    if (!do_full && current_screen == UiScreen::MAIN && (now - last_full_refresh_ms) < MAIN_FULL_REFRESH_MIN_MS) {
        lvgl_eink_commit_update();
        partial_refresh_count++;
    } else {
        lvgl_eink_commit_update();
        last_full_refresh_ms = now;
        partial_refresh_count = 0;
    }

    pending_refresh = RefreshType::NONE;
}

UiScreen ui_current_screen() {
    return current_screen;
}

void ui_init() {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    current_screen = UiScreen::MAIN;
    current_qr_payload = "";

    last_temp_text = "";
    last_hum_text = "";
    last_batt_text = "";
    last_status_text = "";
    last_time_text = "";
    last_env_text = "";
    last_displayed_temp = NAN;
    last_displayed_hum = NAN;
    last_displayed_batt = -1;

    lv_obj_t *cont = lv_obj_create(scr);
    lv_obj_set_size(cont, 296, 128);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_radius(cont, 0, 0);
    lv_obj_set_style_bg_opa(cont, 0, 0);
    lv_obj_center(cont);

    label_temp = lv_label_create(cont);
    lv_obj_set_style_text_font(label_temp, &lv_font_montserrat_40, 0);
    lv_label_set_text(label_temp, "--.-");
    lv_obj_align(label_temp, LV_ALIGN_CENTER, -78, -4);

    lv_obj_t *unit_t = lv_label_create(cont);
    lv_label_set_text(unit_t, "TEMP C");
    lv_obj_set_style_text_font(unit_t, &lv_font_montserrat_14, 0);
    lv_obj_align(unit_t, LV_ALIGN_CENTER, -78, 30);

    label_hum = lv_label_create(cont);
    lv_obj_set_style_text_font(label_hum, &lv_font_montserrat_40, 0);
    lv_label_set_text(label_hum, "--.-");
    lv_obj_align(label_hum, LV_ALIGN_CENTER, 78, -4);

    lv_obj_t *unit_h = lv_label_create(cont);
    lv_label_set_text(unit_h, "HUM %");
    lv_obj_set_style_text_font(unit_h, &lv_font_montserrat_14, 0);
    lv_obj_align(unit_h, LV_ALIGN_CENTER, 78, 30);

    label_batt_pct = lv_label_create(scr);
    lv_obj_set_style_text_font(label_batt_pct, &lv_font_montserrat_12, 0);
    lv_label_set_text(label_batt_pct, "Bat --%");

    label_charge = lv_label_create(scr);
    lv_label_set_text(label_charge, LV_SYMBOL_CHARGE);
    lv_obj_set_style_text_font(label_charge, &lv_font_montserrat_10, 0);
    lv_obj_add_flag(label_charge, LV_OBJ_FLAG_HIDDEN);

    label_wifi = lv_label_create(scr);
    lv_obj_set_style_text_font(label_wifi, &lv_font_montserrat_10, 0);

    label_mqtt = lv_label_create(scr);
    lv_obj_set_style_text_font(label_mqtt, &lv_font_montserrat_10, 0);

    label_ble = lv_label_create(scr);
    lv_label_set_text(label_ble, "BLE");
    lv_obj_set_style_text_font(label_ble, &lv_font_montserrat_10, 0);
    lv_obj_add_flag(label_ble, LV_OBJ_FLAG_HIDDEN);

    label_time = lv_label_create(scr);
    lv_obj_set_style_text_font(label_time, &lv_font_montserrat_14, 0);
    lv_label_set_text(label_time, "--:--");
    lv_obj_align(label_time, LV_ALIGN_TOP_LEFT, 4, 2);

    label_env = lv_label_create(scr);
    lv_obj_set_style_text_font(label_env, &lv_font_montserrat_14, 0);
    lv_label_set_text(label_env, "");
    lv_obj_set_style_border_width(label_env, 0, 0);
    lv_obj_set_style_pad_left(label_env, 0, 0);
    lv_obj_set_style_pad_right(label_env, 0, 0);
    lv_obj_set_style_pad_top(label_env, 0, 0);
    lv_obj_set_style_pad_bottom(label_env, 0, 0);

    ui_set_wifi_status(current_wifi_status);
    ui_set_mqtt_status(current_mqtt_status);
    layout_status_bar();
    lv_obj_invalidate(scr);
    ui_request_refresh(RefreshType::FULL, RefreshReason::SCREEN_SWITCH);
}

void ui_update_sensor_data(float temperature, float humidity) {
    if (current_screen != UiScreen::MAIN || !label_temp || !label_hum) return;

    bool changed = false;
    if (isnan(temperature)) {
        changed |= set_label_text_if_changed(label_temp, last_temp_text, "Err");
    } else if (isnan(last_displayed_temp) || fabsf(temperature - last_displayed_temp) >= 0.2f) {
        char buf[12];
        snprintf(buf, sizeof(buf), "%.1f", temperature);
        changed |= set_label_text_if_changed(label_temp, last_temp_text, buf);
        last_displayed_temp = temperature;
    }

    if (isnan(humidity)) {
        changed |= set_label_text_if_changed(label_hum, last_hum_text, "Err");
    } else if (isnan(last_displayed_hum) || fabsf(humidity - last_displayed_hum) >= 1.0f) {
        char buf[12];
        snprintf(buf, sizeof(buf), "%.1f", humidity);
        changed |= set_label_text_if_changed(label_hum, last_hum_text, buf);
        last_displayed_hum = humidity;
    }

    if (changed) {
        ui_request_refresh(RefreshType::PARTIAL, RefreshReason::SENSOR_CHANGED);
    }
}

void ui_update_battery(float voltage, int percent, bool charging) {
    (void)voltage;
    if (current_screen != UiScreen::MAIN || !label_batt_pct || !label_charge) return;

    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    bool changed = false;
    if (last_displayed_batt < 0 || abs(percent - last_displayed_batt) >= 1) {
        String text = String("Bat ") + String(percent) + "%";
        if (charging) text += "+";
    changed |= set_label_text_if_changed(label_batt_pct, last_batt_text, text);
        last_displayed_batt = percent;
    }

    if (charging != last_displayed_charging) {
        last_displayed_charging = charging;
        String text = String("Bat ") + String(percent) + "%";
        if (charging) text += "+";
        changed |= set_label_text_if_changed(label_batt_pct, last_batt_text, text);
        changed = true;
    }

    if (changed) {
        status_battery_percent = percent;
        status_charging = charging;
        layout_status_bar();
        ui_request_refresh(RefreshType::PARTIAL, RefreshReason::BATTERY_CHANGED);
    }
}

void ui_update_time(const char* time_str) {
    if (current_screen != UiScreen::MAIN || !time_str || !label_time) return;
    if (last_time_text != time_str) {
        last_time_text = time_str;
        lv_label_set_text(label_time, time_str);
        ui_request_refresh(RefreshType::PARTIAL, RefreshReason::NETWORK_CHANGED);
    }
}

void ui_set_wifi_status(WifiStatus status) {
    if (status == current_wifi_status && current_screen == UiScreen::MAIN) return;
    current_wifi_status = status;
    if (current_screen == UiScreen::MAIN && label_wifi) {
        layout_status_bar();
        ui_request_refresh(RefreshType::PARTIAL, RefreshReason::NETWORK_CHANGED);
    }
}

void ui_set_mqtt_status(MqttStatus status) {
    if (status == current_mqtt_status && current_screen == UiScreen::MAIN) return;
    current_mqtt_status = status;
    if (current_screen == UiScreen::MAIN && label_mqtt) {
        layout_status_bar();
        ui_request_refresh(RefreshType::PARTIAL, RefreshReason::NETWORK_CHANGED);
    }
}

void ui_set_provisioning_status(bool active) {
    if (active == provisioning_active && current_screen == UiScreen::MAIN) return;
    provisioning_active = active;
    if (current_screen == UiScreen::MAIN && label_ble) {
        layout_status_bar();
        ui_request_refresh(RefreshType::PARTIAL, RefreshReason::NETWORK_CHANGED);
    }
}

void ui_set_environment_status(const char* text) {
    if (current_screen != UiScreen::MAIN || !label_env || !text) return;
    String next(text);
    if (next == last_env_text) return;
    last_env_text = next;
    lv_label_set_text(label_env, text);
    layout_status_bar();
    ui_request_refresh(RefreshType::PARTIAL, RefreshReason::NETWORK_CHANGED);
}

void ui_update(float temp, float hum, int battery_pct, bool is_charging, int rssi, const char* time_str) {
    if (current_screen != UiScreen::MAIN) return;

    ui_update_sensor_data(temp, hum);
    ui_update_battery(0.0f, battery_pct, is_charging);
    ui_set_wifi_status(rssi == 0 ? WifiStatus::OFFLINE : WifiStatus::CONNECTED);

    ui_update_time(time_str);
}

static void show_qr_screen(const char* qr_data, const char* title_text, const char* hint_text, UiScreen target_screen) {
    if (!qr_data) return;
    String payload(qr_data);
    if (current_screen == target_screen && current_qr_payload == payload) {
        return;
    }

    current_screen = target_screen;
    current_qr_payload = payload;
    label_temp = nullptr;
    label_hum = nullptr;
    label_batt_pct = nullptr;
    label_charge = nullptr;
    label_wifi = nullptr;
    label_mqtt = nullptr;
    label_ble = nullptr;
    label_time = nullptr;
    label_env = nullptr;

    lvgl_eink_clear_panel();

    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    QRCode qrcode;
    const uint8_t qr_version = 5;
    uint8_t qrcode_data[qrcode_getBufferSize(qr_version)];
    qrcode_initText(&qrcode, qrcode_data, qr_version, ECC_LOW, qr_data);

    const int scale = 3;
    const int canvas_size = qrcode.size * scale;
    static lv_color_t canvas_buf[120 * 120];

    lv_obj_t *canvas = lv_canvas_create(scr);
    lv_canvas_set_buffer(canvas, canvas_buf, canvas_size, canvas_size, LV_IMG_CF_TRUE_COLOR);
    lv_canvas_fill_bg(canvas, lv_color_white(), 255);
    lv_obj_align(canvas, LV_ALIGN_LEFT_MID, 8, 0);

    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.bg_color = lv_color_black();

    for (uint8_t y = 0; y < qrcode.size; y++) {
        for (uint8_t x = 0; x < qrcode.size; x++) {
            if (qrcode_getModule(&qrcode, x, y)) {
                lv_canvas_draw_rect(canvas, x * scale, y * scale, scale, scale, &rect_dsc);
            }
        }
    }

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, title_text);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(title, 132, 12);

    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint, hint_text);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_set_width(hint, 156);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(hint, 132, 40);

    lv_obj_t *footer = lv_label_create(scr);
    lv_label_set_text(footer, "BTN: BACK");
    lv_obj_set_style_text_font(footer, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(footer, 132, 104);

    lv_obj_invalidate(scr);
    ui_request_refresh(RefreshType::FULL, RefreshReason::SCREEN_SWITCH);
}

void ui_show_ble_provisioning_qr(const char* qr_data) {
    show_qr_screen(qr_data, "BLE SETUP", "ESP Prov App\nPoP: 12345678", UiScreen::PROVISIONING_QR);
}

void ui_show_web_console_qr(const char* url) {
    show_qr_screen(url, "WEB SETUP", "Same WiFi\nScan to open", UiScreen::WEB_CONSOLE_QR);
}

void ui_display_qrcode(const char* qr_data) {
    ui_show_ble_provisioning_qr(qr_data);
}
