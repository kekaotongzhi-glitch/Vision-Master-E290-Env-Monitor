#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <Arduino.h>

bool network_init();
void network_process();
void network_sync_time();
String network_get_time_str();
int network_get_rssi();
bool network_is_wifi_connected();
bool network_is_mqtt_connected();
bool network_report(float temp, float hum, int batt, float batt_voltage, const char* report_reason = "interval", bool force_stale = false);
bool network_report_cached(const char* report_reason, bool force_stale = false);
bool network_report_now_requested();
void network_clear_report_now_request();
void network_finish_report_now(bool telemetry_ok);
bool network_wifi_profile_save(const String& ssid, const String& password, bool password_provided, int priority, bool enabled);
bool network_wifi_profile_delete(const String& ssid);
String network_wifi_profiles_json();
void network_request_wifi_reconnect();
void start_softap_fallback_portal();

#endif
