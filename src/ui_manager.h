#ifndef UI_MANAGER_H
#define UI_MANAGER_H

#include <Arduino.h>

enum class WifiStatus {
    UNCONFIGURED,
    CONNECTING,
    CONNECTED,
    OFFLINE
};

enum class MqttStatus {
    DISCONNECTED,
    CONNECTING,
    CONNECTED
};

enum class UiScreen {
    MAIN,
    PROVISIONING_QR,
    WEB_CONSOLE_QR,
    ERROR
};

enum class RefreshType {
    NONE,
    PARTIAL,
    FULL
};

enum class RefreshReason {
    SCREEN_SWITCH,
    SENSOR_CHANGED,
    BATTERY_CHANGED,
    NETWORK_CHANGED,
    PERIODIC_GHOST_CLEANUP
};

void ui_init();
void ui_update(float temp, float hum, int battery_pct, bool is_charging, int rssi, const char* time_str);
void ui_update_sensor_data(float temperature, float humidity);
void ui_update_battery(float voltage, int percent, bool charging);
void ui_update_time(const char* time_str);
void ui_set_wifi_status(WifiStatus status);
void ui_set_mqtt_status(MqttStatus status);
void ui_set_provisioning_status(bool active);
void ui_set_environment_status(const char* text);
void ui_request_refresh(RefreshType type, RefreshReason reason);
void ui_commit_refresh_if_needed();
UiScreen ui_current_screen();
void ui_show_ble_provisioning_qr(const char* qr_data);
void ui_show_web_console_qr(const char* url);
void ui_display_qrcode(const char* qr_data);

#endif
