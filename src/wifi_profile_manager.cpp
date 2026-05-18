#include "wifi_profile_manager.h"
#include <Preferences.h>
#include <WiFi.h>

static const char* WIFI_PROFILE_NAMESPACE = "wifi_profiles";

static Preferences prefs;
static WifiProfile profiles[WIFI_PROFILE_MAX];
static size_t profile_count = 0;
static bool initialized = false;

static bool valid_ssid(const String& ssid) {
    return ssid.length() > 0 && ssid.length() <= 32;
}

static int clamp_priority(int priority) {
    if (priority < 0) return 0;
    if (priority > 100) return 100;
    return priority;
}

static int find_profile_index(const String& ssid) {
    for (size_t i = 0; i < profile_count; ++i) {
        if (profiles[i].ssid == ssid) return static_cast<int>(i);
    }
    return -1;
}

static void sort_profiles(WifiProfile* items, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        for (size_t j = i + 1; j < count; ++j) {
            bool better_priority = items[j].priority > items[i].priority;
            bool same_priority_better_rssi = items[j].priority == items[i].priority && items[j].rssi > items[i].rssi;
            if (better_priority || same_priority_better_rssi) {
                WifiProfile tmp = items[i];
                items[i] = items[j];
                items[j] = tmp;
            }
        }
    }
}

bool wifi_profiles_init() {
    if (initialized) return true;
    initialized = wifi_profiles_load();
    return initialized;
}

size_t wifi_profiles_count() {
    return profile_count;
}

bool wifi_profiles_add(const String& ssid, const String& password, int priority) {
    return wifi_profiles_save_profile(ssid, password, true, priority, true);
}

bool wifi_profiles_save_profile(const String& ssid, const String& password, bool password_provided, int priority, bool enabled) {
    if (!wifi_profiles_init()) return false;
    if (!valid_ssid(ssid)) return false;

    int idx = find_profile_index(ssid);
    if (idx < 0) {
        if (profile_count >= WIFI_PROFILE_MAX) return false;
        idx = static_cast<int>(profile_count++);
        profiles[idx].password = "";
    }

    profiles[idx].ssid = ssid;
    if (password_provided) {
        profiles[idx].password = password;
    }
    profiles[idx].priority = clamp_priority(priority);
    profiles[idx].enabled = enabled;
    profiles[idx].rssi = 0;
    return wifi_profiles_save();
}

bool wifi_profiles_remove(const String& ssid) {
    if (!wifi_profiles_init()) return false;

    int idx = find_profile_index(ssid);
    if (idx < 0) return false;

    for (size_t i = static_cast<size_t>(idx); i + 1 < profile_count; ++i) {
        profiles[i] = profiles[i + 1];
    }
    profile_count--;
    return wifi_profiles_save();
}

bool wifi_profiles_clear() {
    if (!wifi_profiles_init()) return false;
    profile_count = 0;
    return wifi_profiles_save();
}

size_t wifi_profiles_get_all(WifiProfile* out_profiles, size_t max_profiles) {
    if (!wifi_profiles_init() || !out_profiles) return 0;
    size_t count = profile_count < max_profiles ? profile_count : max_profiles;
    for (size_t i = 0; i < count; ++i) {
        out_profiles[i] = profiles[i];
    }
    return count;
}

size_t wifi_profiles_select_available(WifiProfile* out_profiles, size_t max_profiles) {
    if (!wifi_profiles_init() || !out_profiles || max_profiles == 0 || profile_count == 0) return 0;

    WiFi.mode(WIFI_STA);
    int found = WiFi.scanNetworks(false, true);
    if (found <= 0) {
        WiFi.scanDelete();
        return 0;
    }

    size_t out_count = 0;
    for (size_t i = 0; i < profile_count && out_count < max_profiles; ++i) {
        if (!profiles[i].enabled) continue;

        int best_rssi = -127;
        bool available = false;

        for (int ap = 0; ap < found; ++ap) {
            if (WiFi.SSID(ap) == profiles[i].ssid) {
                available = true;
                if (WiFi.RSSI(ap) > best_rssi) best_rssi = WiFi.RSSI(ap);
            }
        }

        if (available) {
            out_profiles[out_count] = profiles[i];
            out_profiles[out_count].rssi = best_rssi;
            out_count++;
        }
    }

    WiFi.scanDelete();
    sort_profiles(out_profiles, out_count);
    return out_count;
}

bool wifi_profiles_select_best_available(WifiProfile& selected) {
    WifiProfile candidates[WIFI_PROFILE_MAX];
    size_t count = wifi_profiles_select_available(candidates, WIFI_PROFILE_MAX);
    if (count == 0) return false;
    selected = candidates[0];
    return true;
}

bool wifi_profiles_save() {
    if (!prefs.begin(WIFI_PROFILE_NAMESPACE, false)) return false;

    prefs.clear();
    prefs.putUInt("count", static_cast<uint32_t>(profile_count));

    bool ok = true;
    for (size_t i = 0; i < profile_count; ++i) {
        String suffix = String(i);
        ok &= prefs.putString(("ssid" + suffix).c_str(), profiles[i].ssid) > 0;
        ok &= prefs.putString(("pass" + suffix).c_str(), profiles[i].password) > 0 || profiles[i].password.length() == 0;
        ok &= prefs.putInt(("prio" + suffix).c_str(), profiles[i].priority) > 0 || profiles[i].priority == 0;
        ok &= prefs.putBool(("en" + suffix).c_str(), profiles[i].enabled);
    }

    prefs.end();
    return ok;
}

bool wifi_profiles_load() {
    if (!prefs.begin(WIFI_PROFILE_NAMESPACE, false)) return false;

    profile_count = 0;
    uint32_t saved_count = prefs.getUInt("count", 0);
    if (saved_count > WIFI_PROFILE_MAX) saved_count = WIFI_PROFILE_MAX;

    for (uint32_t i = 0; i < saved_count; ++i) {
        String suffix = String(i);
        String ssid = prefs.getString(("ssid" + suffix).c_str(), "");
        if (!valid_ssid(ssid)) continue;

        profiles[profile_count].ssid = ssid;
        profiles[profile_count].password = prefs.getString(("pass" + suffix).c_str(), "");
        profiles[profile_count].priority = clamp_priority(prefs.getInt(("prio" + suffix).c_str(), 50));
        profiles[profile_count].enabled = prefs.getBool(("en" + suffix).c_str(), true);
        profiles[profile_count].rssi = 0;
        profile_count++;
    }

    prefs.end();
    return true;
}
