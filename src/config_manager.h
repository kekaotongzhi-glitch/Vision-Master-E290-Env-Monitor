#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Arduino.h>

static const uint32_t DHT11_SAFE_MIN_SAMPLE_MS = 2000;
static const uint32_t REPORT_SAFE_MIN_INTERVAL_MS = DHT11_SAFE_MIN_SAMPLE_MS;
static const uint32_t DISPLAY_SAFE_MIN_REFRESH_MS = 5000;
static const size_t MAX_ENV_PROFILES = 8;

struct EnvProfile {
    String id;
    String name;
    float temp_normal_min;
    float temp_normal_max;
    float temp_alarm_min;
    float temp_alarm_max;
    float humi_normal_min;
    float humi_normal_max;
    float humi_alarm_min;
    float humi_alarm_max;
};

struct AppConfig {
    String device_id;
    String device_name;
    String mqtt_host;
    uint16_t mqtt_port;
    String mqtt_client_id;
    String mqtt_username;
    String mqtt_password;
    String topic_prefix;
    String bemfa_uid;
    String mqtt_topic_temp;
    String mqtt_topic_humi;
    String mqtt_topic_battery;
    uint32_t sensor_sample_interval_ms;
    uint32_t report_interval_ms;
    uint32_t display_refresh_interval_ms;
    bool report_enabled;
    bool display_enabled;
    String environment_preset;
    String active_env_profile_id;
    String active_env_profile_name;
    uint8_t env_profile_count;
    float temp_normal_min;
    float temp_normal_max;
    float temp_alarm_min;
    float temp_alarm_max;
    float humi_normal_min;
    float humi_normal_max;
    float humi_alarm_min;
    float humi_alarm_max;
    uint32_t sample_interval_ms;
    uint32_t upload_interval_ms;
    float battery_voltage_factor;
};

bool config_init();
AppConfig config_get();
bool config_save(const AppConfig& cfg);
void config_reset_to_default();
bool config_is_valid();
AppConfig config_default();
AppConfig config_normalize(const AppConfig& cfg);
bool config_validate_thresholds(const AppConfig& cfg);
uint32_t config_get_effective_sample_interval_ms();
bool config_validate_env_profile(const EnvProfile& profile);
size_t config_env_profiles_get_all(EnvProfile* out, size_t max_count);
bool config_env_profile_get_active(EnvProfile& profile);
bool config_env_profile_find(const String& id_or_name, EnvProfile& profile);
bool config_env_profile_select(const String& id);
bool config_env_profile_save(EnvProfile profile, EnvProfile* saved_profile = nullptr);
bool config_env_profile_delete(const String& id);
String config_env_profiles_json();

#endif
