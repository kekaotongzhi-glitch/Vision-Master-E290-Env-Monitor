#ifndef WIFI_PROFILE_MANAGER_H
#define WIFI_PROFILE_MANAGER_H

#include <Arduino.h>

struct WifiProfile {
    String ssid;
    String password;
    int priority;
    int rssi;
    bool enabled;
};

static const size_t WIFI_PROFILE_MAX = 5;

bool wifi_profiles_init();
size_t wifi_profiles_count();
bool wifi_profiles_add(const String& ssid, const String& password, int priority);
bool wifi_profiles_save_profile(const String& ssid, const String& password, bool password_provided, int priority, bool enabled);
bool wifi_profiles_remove(const String& ssid);
bool wifi_profiles_clear();
bool wifi_profiles_select_best_available(WifiProfile& selected);
size_t wifi_profiles_get_all(WifiProfile* profiles, size_t max_profiles);
size_t wifi_profiles_select_available(WifiProfile* profiles, size_t max_profiles);
bool wifi_profiles_save();
bool wifi_profiles_load();

#endif
