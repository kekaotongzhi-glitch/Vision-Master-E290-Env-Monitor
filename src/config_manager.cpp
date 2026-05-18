#include "config_manager.h"
#include <Preferences.h>

static const char* CONFIG_NAMESPACE = "app_cfg";

static Preferences prefs;
static AppConfig current_config;
static bool initialized = false;
static EnvProfile env_profiles[MAX_ENV_PROFILES];
static uint8_t env_profile_count = 0;
static String active_env_profile_id = "home";
static uint16_t env_profile_next_id = 1;

static void apply_environment_preset(AppConfig& cfg, const String& preset) {
    if (preset == "farm") {
        cfg.environment_preset = "farm";
        cfg.temp_normal_min = 15.0f;
        cfg.temp_normal_max = 30.0f;
        cfg.temp_alarm_min = 5.0f;
        cfg.temp_alarm_max = 38.0f;
        cfg.humi_normal_min = 50.0f;
        cfg.humi_normal_max = 80.0f;
        cfg.humi_alarm_min = 30.0f;
        cfg.humi_alarm_max = 90.0f;
    } else {
        cfg.environment_preset = "home";
        cfg.temp_normal_min = 18.0f;
        cfg.temp_normal_max = 28.0f;
        cfg.temp_alarm_min = 5.0f;
        cfg.temp_alarm_max = 35.0f;
        cfg.humi_normal_min = 40.0f;
        cfg.humi_normal_max = 70.0f;
        cfg.humi_alarm_min = 20.0f;
        cfg.humi_alarm_max = 85.0f;
    }
}

static EnvProfile make_home_profile() {
    EnvProfile profile;
    profile.id = "home";
    profile.name = "家";
    profile.temp_normal_min = 18.0f;
    profile.temp_normal_max = 28.0f;
    profile.temp_alarm_min = 5.0f;
    profile.temp_alarm_max = 35.0f;
    profile.humi_normal_min = 40.0f;
    profile.humi_normal_max = 70.0f;
    profile.humi_alarm_min = 20.0f;
    profile.humi_alarm_max = 85.0f;
    return profile;
}

static EnvProfile make_farm_profile() {
    EnvProfile profile;
    profile.id = "farm";
    profile.name = "农场";
    profile.temp_normal_min = 15.0f;
    profile.temp_normal_max = 30.0f;
    profile.temp_alarm_min = 5.0f;
    profile.temp_alarm_max = 38.0f;
    profile.humi_normal_min = 50.0f;
    profile.humi_normal_max = 80.0f;
    profile.humi_alarm_min = 30.0f;
    profile.humi_alarm_max = 90.0f;
    return profile;
}

static EnvProfile make_default_profile() {
    EnvProfile profile = make_home_profile();
    profile.id = "default";
    profile.name = "默认";
    return profile;
}

static void copy_profile_to_config(AppConfig& cfg, const EnvProfile& profile) {
    cfg.active_env_profile_id = profile.id;
    cfg.active_env_profile_name = profile.name;
    cfg.environment_preset = profile.name.length() ? profile.name : profile.id;
    cfg.env_profile_count = env_profile_count;
    cfg.temp_normal_min = profile.temp_normal_min;
    cfg.temp_normal_max = profile.temp_normal_max;
    cfg.temp_alarm_min = profile.temp_alarm_min;
    cfg.temp_alarm_max = profile.temp_alarm_max;
    cfg.humi_normal_min = profile.humi_normal_min;
    cfg.humi_normal_max = profile.humi_normal_max;
    cfg.humi_alarm_min = profile.humi_alarm_min;
    cfg.humi_alarm_max = profile.humi_alarm_max;
}

static int find_profile_index_by_id(const String& id) {
    for (uint8_t i = 0; i < env_profile_count; ++i) {
        if (env_profiles[i].id == id) return i;
    }
    return -1;
}

static int find_profile_index_by_id_or_name(const String& value) {
    for (uint8_t i = 0; i < env_profile_count; ++i) {
        if (env_profiles[i].id == value || env_profiles[i].name == value) return i;
    }
    if (value == "home") {
        for (uint8_t i = 0; i < env_profile_count; ++i) {
            if (env_profiles[i].name == "家") return i;
        }
    }
    if (value == "farm") {
        for (uint8_t i = 0; i < env_profile_count; ++i) {
            if (env_profiles[i].name == "农场") return i;
        }
    }
    return -1;
}

static String sanitize_profile_id(String id) {
    id.trim();
    id.toLowerCase();
    String out;
    for (uint16_t i = 0; i < id.length(); ++i) {
        char c = id[i];
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_') {
            out += c;
        }
    }
    return out;
}

static String next_profile_id() {
    String id;
    do {
        id = "profile" + String(env_profile_next_id++);
    } while (find_profile_index_by_id(id) >= 0);
    return id;
}

static bool load_env_profiles_from_nvs() {
    env_profile_count = prefs.getUChar("envp_count", 0);
    if (env_profile_count > MAX_ENV_PROFILES) env_profile_count = 0;
    active_env_profile_id = prefs.getString("env_active", "home");
    env_profile_next_id = prefs.getUShort("env_next", 1);

    for (uint8_t i = 0; i < env_profile_count; ++i) {
        String prefix = "ep" + String(i) + "_";
        env_profiles[i].id = prefs.getString((prefix + "id").c_str(), "");
        env_profiles[i].name = prefs.getString((prefix + "name").c_str(), env_profiles[i].id);
        env_profiles[i].temp_normal_min = prefs.getFloat((prefix + "tnmin").c_str(), 18.0f);
        env_profiles[i].temp_normal_max = prefs.getFloat((prefix + "tnmax").c_str(), 28.0f);
        env_profiles[i].temp_alarm_min = prefs.getFloat((prefix + "tamin").c_str(), 5.0f);
        env_profiles[i].temp_alarm_max = prefs.getFloat((prefix + "tamax").c_str(), 35.0f);
        env_profiles[i].humi_normal_min = prefs.getFloat((prefix + "hnmin").c_str(), 40.0f);
        env_profiles[i].humi_normal_max = prefs.getFloat((prefix + "hnmax").c_str(), 70.0f);
        env_profiles[i].humi_alarm_min = prefs.getFloat((prefix + "hamin").c_str(), 20.0f);
        env_profiles[i].humi_alarm_max = prefs.getFloat((prefix + "hamax").c_str(), 85.0f);
        if (env_profiles[i].id.length() == 0 || !config_validate_env_profile(env_profiles[i])) {
            env_profile_count = 0;
            return false;
        }
    }
    return env_profile_count > 0;
}

static bool save_env_profiles_to_nvs() {
    bool ok = true;
    ok &= prefs.putUChar("envp_count", env_profile_count) > 0;
    ok &= prefs.putString("env_active", active_env_profile_id) > 0;
    ok &= prefs.putUShort("env_next", env_profile_next_id) > 0;
    for (uint8_t i = 0; i < MAX_ENV_PROFILES; ++i) {
        String prefix = "ep" + String(i) + "_";
        if (i < env_profile_count) {
            const EnvProfile& p = env_profiles[i];
            ok &= prefs.putString((prefix + "id").c_str(), p.id) > 0;
            ok &= prefs.putString((prefix + "name").c_str(), p.name) > 0;
            ok &= prefs.putFloat((prefix + "tnmin").c_str(), p.temp_normal_min) > 0;
            ok &= prefs.putFloat((prefix + "tnmax").c_str(), p.temp_normal_max) > 0;
            ok &= prefs.putFloat((prefix + "tamin").c_str(), p.temp_alarm_min) > 0;
            ok &= prefs.putFloat((prefix + "tamax").c_str(), p.temp_alarm_max) > 0;
            ok &= prefs.putFloat((prefix + "hnmin").c_str(), p.humi_normal_min) > 0;
            ok &= prefs.putFloat((prefix + "hnmax").c_str(), p.humi_normal_max) > 0;
            ok &= prefs.putFloat((prefix + "hamin").c_str(), p.humi_alarm_min) > 0;
            ok &= prefs.putFloat((prefix + "hamax").c_str(), p.humi_alarm_max) > 0;
        } else {
            prefs.remove((prefix + "id").c_str());
            prefs.remove((prefix + "name").c_str());
        }
    }
    return ok;
}

static void ensure_default_env_profiles() {
    if (env_profile_count > 0) return;
    env_profiles[0] = make_home_profile();
    env_profiles[1] = make_farm_profile();
    env_profile_count = 2;
    active_env_profile_id = "home";
    env_profile_next_id = 1;
    save_env_profiles_to_nvs();
}

static void sync_active_profile_to_config(AppConfig& cfg) {
    ensure_default_env_profiles();
    int idx = find_profile_index_by_id(active_env_profile_id);
    if (idx < 0) {
        idx = 0;
        active_env_profile_id = env_profiles[0].id;
        save_env_profiles_to_nvs();
    }
    copy_profile_to_config(cfg, env_profiles[idx]);
}

AppConfig config_default() {
    AppConfig cfg;
    cfg.device_id = "eink-sensor";
    cfg.device_name = "eink-sensor";
    cfg.mqtt_host = "";
    cfg.mqtt_port = 1883;
    cfg.mqtt_client_id = "";
    cfg.mqtt_username = "";
    cfg.mqtt_password = "";
    cfg.topic_prefix = "devices/eink-sensor";
    cfg.bemfa_uid = "";
    cfg.mqtt_topic_temp = "temp002";
    cfg.mqtt_topic_humi = "humi002";
    cfg.mqtt_topic_battery = "batt002";
    cfg.sensor_sample_interval_ms = 10000;
    cfg.report_interval_ms = 60000;
    cfg.display_refresh_interval_ms = 50000;
    cfg.report_enabled = true;
    cfg.display_enabled = true;
    apply_environment_preset(cfg, "home");
    cfg.active_env_profile_id = "home";
    cfg.active_env_profile_name = "家";
    cfg.env_profile_count = 2;
    cfg.sample_interval_ms = 10000;
    cfg.upload_interval_ms = 60000;
    cfg.battery_voltage_factor = 4.9f;
    return cfg;
}

AppConfig config_normalize(const AppConfig& cfg) {
    AppConfig out = cfg;
    AppConfig defaults = config_default();

    if (out.sensor_sample_interval_ms == 0) {
        out.sensor_sample_interval_ms = out.sample_interval_ms ? out.sample_interval_ms : DHT11_SAFE_MIN_SAMPLE_MS;
    }
    if (out.report_interval_ms == 0) {
        out.report_interval_ms = out.upload_interval_ms ? out.upload_interval_ms : REPORT_SAFE_MIN_INTERVAL_MS;
    }
    if (out.display_refresh_interval_ms == 0) {
        out.display_refresh_interval_ms = defaults.display_refresh_interval_ms;
    }

    if (out.sensor_sample_interval_ms < DHT11_SAFE_MIN_SAMPLE_MS) {
        out.sensor_sample_interval_ms = DHT11_SAFE_MIN_SAMPLE_MS;
    }
    if (out.report_interval_ms < REPORT_SAFE_MIN_INTERVAL_MS) {
        out.report_interval_ms = REPORT_SAFE_MIN_INTERVAL_MS;
    }
    if (out.display_refresh_interval_ms < DISPLAY_SAFE_MIN_REFRESH_MS) {
        out.display_refresh_interval_ms = DISPLAY_SAFE_MIN_REFRESH_MS;
    }

    if (out.report_enabled && out.report_interval_ms < out.sensor_sample_interval_ms) {
        out.sensor_sample_interval_ms = out.report_interval_ms;
    }
    if (out.display_enabled && out.display_refresh_interval_ms < out.sensor_sample_interval_ms) {
        out.sensor_sample_interval_ms = out.display_refresh_interval_ms;
    }
    if (out.sensor_sample_interval_ms < DHT11_SAFE_MIN_SAMPLE_MS) {
        out.sensor_sample_interval_ms = DHT11_SAFE_MIN_SAMPLE_MS;
    }

    if (out.active_env_profile_id.length() == 0) out.active_env_profile_id = active_env_profile_id;
    if (out.active_env_profile_name.length() == 0) out.active_env_profile_name = out.environment_preset;
    out.env_profile_count = env_profile_count;
    out.sample_interval_ms = out.sensor_sample_interval_ms;
    out.upload_interval_ms = out.report_interval_ms;
    return out;
}

static bool validate_config(const AppConfig& cfg) {
    if (cfg.device_id.length() == 0 || cfg.device_id.length() > 48) return false;
    if (cfg.device_name.length() == 0 || cfg.device_name.length() > 31) return false;
    if (cfg.mqtt_host.length() > 63) return false;
    if (cfg.mqtt_port == 0) return false;
    if (cfg.mqtt_client_id.length() > 64) return false;
    if (cfg.mqtt_username.length() > 64) return false;
    if (cfg.mqtt_password.length() > 96) return false;
    if (cfg.topic_prefix.length() == 0 || cfg.topic_prefix.length() > 96) return false;
    if (cfg.mqtt_topic_temp.length() == 0) return false;
    if (cfg.mqtt_topic_humi.length() == 0) return false;
    if (cfg.mqtt_topic_battery.length() == 0) return false;
    if (cfg.sensor_sample_interval_ms < DHT11_SAFE_MIN_SAMPLE_MS) return false;
    if (cfg.report_interval_ms < REPORT_SAFE_MIN_INTERVAL_MS) return false;
    if (cfg.display_refresh_interval_ms < DISPLAY_SAFE_MIN_REFRESH_MS) return false;
    if (cfg.sample_interval_ms < DHT11_SAFE_MIN_SAMPLE_MS) return false;
    if (cfg.upload_interval_ms < REPORT_SAFE_MIN_INTERVAL_MS) return false;
    if (!config_validate_thresholds(cfg)) return false;
    if (cfg.battery_voltage_factor < 1.0f || cfg.battery_voltage_factor > 20.0f) return false;
    return true;
}

bool config_validate_thresholds(const AppConfig& cfg) {
    return cfg.temp_alarm_min <= cfg.temp_normal_min &&
           cfg.temp_normal_min <= cfg.temp_normal_max &&
           cfg.temp_normal_max <= cfg.temp_alarm_max &&
           cfg.humi_alarm_min <= cfg.humi_normal_min &&
           cfg.humi_normal_min <= cfg.humi_normal_max &&
           cfg.humi_normal_max <= cfg.humi_alarm_max;
}

bool config_validate_env_profile(const EnvProfile& profile) {
    return profile.id.length() > 0 &&
           profile.id.length() <= 24 &&
           profile.name.length() > 0 &&
           profile.name.length() <= 32 &&
           profile.temp_alarm_min <= profile.temp_normal_min &&
           profile.temp_normal_min <= profile.temp_normal_max &&
           profile.temp_normal_max <= profile.temp_alarm_max &&
           profile.humi_alarm_min <= profile.humi_normal_min &&
           profile.humi_normal_min <= profile.humi_normal_max &&
           profile.humi_normal_max <= profile.humi_alarm_max;
}

bool config_init() {
    AppConfig defaults = config_default();
    if (!prefs.begin(CONFIG_NAMESPACE, false)) {
        current_config = defaults;
        initialized = false;
        Serial.println("[配置] NVS 打开失败，临时使用默认配置");
        return false;
    }

    if (!load_env_profiles_from_nvs()) {
        env_profile_count = 0;
        ensure_default_env_profiles();
    }

    current_config.device_id = prefs.getString("device_id", defaults.device_id);
    current_config.device_name = prefs.getString("dev_name", defaults.device_name);
    current_config.mqtt_host = prefs.getString("mqtt_host", defaults.mqtt_host);
    current_config.mqtt_port = prefs.getUShort("mqtt_port", defaults.mqtt_port);
    current_config.mqtt_client_id = prefs.getString("mqtt_cid", defaults.mqtt_client_id);
    current_config.mqtt_username = prefs.getString("mqtt_user", defaults.mqtt_username);
    current_config.mqtt_password = prefs.getString("mqtt_pass", defaults.mqtt_password);
    current_config.topic_prefix = prefs.getString("topic_prefix", defaults.topic_prefix);
    current_config.bemfa_uid = prefs.getString("bemfa_uid", defaults.bemfa_uid);
    current_config.mqtt_topic_temp = prefs.getString("topic_temp", defaults.mqtt_topic_temp);
    current_config.mqtt_topic_humi = prefs.getString("topic_humi", defaults.mqtt_topic_humi);
    current_config.mqtt_topic_battery = prefs.getString("topic_batt", defaults.mqtt_topic_battery);
    current_config.sample_interval_ms = prefs.getUInt("sample_ms", defaults.sample_interval_ms);
    current_config.upload_interval_ms = prefs.getUInt("upload_ms", defaults.upload_interval_ms);
    current_config.sensor_sample_interval_ms = prefs.getUInt("sensor_ms", current_config.sample_interval_ms);
    current_config.report_interval_ms = prefs.getUInt("report_ms", current_config.upload_interval_ms);
    current_config.display_refresh_interval_ms = prefs.getUInt("display_ms", defaults.display_refresh_interval_ms);
    current_config.report_enabled = prefs.getBool("report_en", defaults.report_enabled);
    current_config.display_enabled = prefs.getBool("display_en", defaults.display_enabled);
    current_config.environment_preset = prefs.getString("env_preset", defaults.environment_preset);
    current_config.active_env_profile_id = active_env_profile_id;
    current_config.active_env_profile_name = "";
    current_config.env_profile_count = env_profile_count;
    current_config.temp_normal_min = prefs.getFloat("tn_min", defaults.temp_normal_min);
    current_config.temp_normal_max = prefs.getFloat("tn_max", defaults.temp_normal_max);
    current_config.temp_alarm_min = prefs.getFloat("ta_min", defaults.temp_alarm_min);
    current_config.temp_alarm_max = prefs.getFloat("ta_max", defaults.temp_alarm_max);
    current_config.humi_normal_min = prefs.getFloat("hn_min", defaults.humi_normal_min);
    current_config.humi_normal_max = prefs.getFloat("hn_max", defaults.humi_normal_max);
    current_config.humi_alarm_min = prefs.getFloat("ha_min", defaults.humi_alarm_min);
    current_config.humi_alarm_max = prefs.getFloat("ha_max", defaults.humi_alarm_max);
    current_config.battery_voltage_factor = prefs.getFloat("batt_factor", defaults.battery_voltage_factor);
    current_config = config_normalize(current_config);
    sync_active_profile_to_config(current_config);

    if (!validate_config(current_config)) {
        Serial.println("[配置] 已保存配置无效，恢复默认配置");
        current_config = defaults;
        sync_active_profile_to_config(current_config);
        config_save(current_config);
    }

    initialized = true;
    return true;
}

AppConfig config_get() {
    if (!initialized) {
        return current_config.device_name.length() ? current_config : config_default();
    }
    return current_config;
}

bool config_save(const AppConfig& cfg) {
    AppConfig normalized = config_normalize(cfg);
    ensure_default_env_profiles();

    int active_index = normalized.active_env_profile_id.length() ? find_profile_index_by_id(normalized.active_env_profile_id) : find_profile_index_by_id(active_env_profile_id);
    if (active_index >= 0) {
        active_env_profile_id = env_profiles[active_index].id;
    } else {
        active_index = find_profile_index_by_id(active_env_profile_id);
    }
    if (active_index < 0) {
        active_index = 0;
        active_env_profile_id = env_profiles[0].id;
    }

    EnvProfile active_profile = env_profiles[active_index];
    if (normalized.active_env_profile_name.length() > 0) {
        active_profile.name = normalized.active_env_profile_name;
    }
    active_profile.temp_normal_min = normalized.temp_normal_min;
    active_profile.temp_normal_max = normalized.temp_normal_max;
    active_profile.temp_alarm_min = normalized.temp_alarm_min;
    active_profile.temp_alarm_max = normalized.temp_alarm_max;
    active_profile.humi_normal_min = normalized.humi_normal_min;
    active_profile.humi_normal_max = normalized.humi_normal_max;
    active_profile.humi_alarm_min = normalized.humi_alarm_min;
    active_profile.humi_alarm_max = normalized.humi_alarm_max;

    if (!config_validate_env_profile(active_profile)) {
        Serial.println("[配置] 环境预设无效，拒绝保存");
        return false;
    }
    env_profiles[active_index] = active_profile;
    copy_profile_to_config(normalized, active_profile);

    if (!validate_config(normalized)) {
        Serial.println("[配置] 配置无效，拒绝保存");
        return false;
    }

    if (!initialized && !prefs.begin(CONFIG_NAMESPACE, false)) {
        Serial.println("[配置] 保存时 NVS 打开失败");
        return false;
    }

    bool ok = true;
    ok &= prefs.putString("device_id", normalized.device_id) > 0;
    ok &= prefs.putString("dev_name", normalized.device_name) > 0;
    ok &= prefs.putString("mqtt_host", normalized.mqtt_host) >= 0;
    ok &= prefs.putUShort("mqtt_port", normalized.mqtt_port) > 0;
    ok &= prefs.putString("mqtt_cid", normalized.mqtt_client_id) >= 0;
    ok &= prefs.putString("mqtt_user", normalized.mqtt_username) >= 0;
    ok &= prefs.putString("mqtt_pass", normalized.mqtt_password) >= 0;
    ok &= prefs.putString("topic_prefix", normalized.topic_prefix) > 0;
    ok &= prefs.putString("bemfa_uid", normalized.bemfa_uid) >= 0;
    ok &= prefs.putString("topic_temp", normalized.mqtt_topic_temp) > 0;
    ok &= prefs.putString("topic_humi", normalized.mqtt_topic_humi) > 0;
    ok &= prefs.putString("topic_batt", normalized.mqtt_topic_battery) > 0;
    ok &= prefs.putUInt("sensor_ms", normalized.sensor_sample_interval_ms) > 0;
    ok &= prefs.putUInt("report_ms", normalized.report_interval_ms) > 0;
    ok &= prefs.putUInt("display_ms", normalized.display_refresh_interval_ms) > 0;
    ok &= prefs.putBool("report_en", normalized.report_enabled);
    ok &= prefs.putBool("display_en", normalized.display_enabled);
    ok &= prefs.putString("env_preset", normalized.environment_preset) > 0;
    ok &= prefs.putFloat("tn_min", normalized.temp_normal_min) > 0;
    ok &= prefs.putFloat("tn_max", normalized.temp_normal_max) > 0;
    ok &= prefs.putFloat("ta_min", normalized.temp_alarm_min) > 0;
    ok &= prefs.putFloat("ta_max", normalized.temp_alarm_max) > 0;
    ok &= prefs.putFloat("hn_min", normalized.humi_normal_min) > 0;
    ok &= prefs.putFloat("hn_max", normalized.humi_normal_max) > 0;
    ok &= prefs.putFloat("ha_min", normalized.humi_alarm_min) > 0;
    ok &= prefs.putFloat("ha_max", normalized.humi_alarm_max) > 0;
    ok &= prefs.putUInt("sample_ms", normalized.sample_interval_ms) > 0;
    ok &= prefs.putUInt("upload_ms", normalized.upload_interval_ms) > 0;
    ok &= prefs.putFloat("batt_factor", normalized.battery_voltage_factor) > 0;
    ok &= save_env_profiles_to_nvs();

    if (ok) {
        current_config = normalized;
        initialized = true;
        Serial.println("[配置] 已保存");
    } else {
        Serial.println("[配置] 保存失败");
    }
    return ok;
}

void config_reset_to_default() {
    env_profile_count = 0;
    ensure_default_env_profiles();
    AppConfig defaults = config_default();
    sync_active_profile_to_config(defaults);
    config_save(defaults);
}

bool config_is_valid() {
    return validate_config(current_config);
}

size_t config_env_profiles_get_all(EnvProfile* out, size_t max_count) {
    ensure_default_env_profiles();
    size_t count = min(static_cast<size_t>(env_profile_count), max_count);
    for (size_t i = 0; i < count; ++i) {
        out[i] = env_profiles[i];
    }
    return count;
}

bool config_env_profile_get_active(EnvProfile& profile) {
    ensure_default_env_profiles();
    int idx = find_profile_index_by_id(active_env_profile_id);
    if (idx < 0) idx = 0;
    profile = env_profiles[idx];
    return true;
}

bool config_env_profile_find(const String& id_or_name, EnvProfile& profile) {
    ensure_default_env_profiles();
    int idx = find_profile_index_by_id_or_name(id_or_name);
    if (idx < 0) return false;
    profile = env_profiles[idx];
    return true;
}

bool config_env_profile_select(const String& id) {
    ensure_default_env_profiles();
    int idx = find_profile_index_by_id_or_name(id);
    if (idx < 0) return false;
    active_env_profile_id = env_profiles[idx].id;
    copy_profile_to_config(current_config, env_profiles[idx]);
    save_env_profiles_to_nvs();
    prefs.putString("env_preset", current_config.environment_preset);
    prefs.putString("env_active_id", active_env_profile_id);
    return true;
}

bool config_env_profile_save(EnvProfile profile, EnvProfile* saved_profile) {
    ensure_default_env_profiles();
    profile.id = sanitize_profile_id(profile.id);
    profile.name.trim();
    if (profile.id.length() == 0) {
        profile.id = next_profile_id();
    }
    if (profile.name.length() == 0) {
        profile.name = profile.id;
    }
    if (!config_validate_env_profile(profile)) return false;

    int idx = find_profile_index_by_id(profile.id);
    if (idx < 0) {
        if (env_profile_count >= MAX_ENV_PROFILES) return false;
        idx = env_profile_count++;
    }
    env_profiles[idx] = profile;
    if (active_env_profile_id.length() == 0) active_env_profile_id = profile.id;
    current_config.env_profile_count = env_profile_count;
    if (active_env_profile_id == profile.id) {
        copy_profile_to_config(current_config, profile);
    }
    if (!save_env_profiles_to_nvs()) return false;
    if (saved_profile) *saved_profile = profile;
    return true;
}

bool config_env_profile_delete(const String& id) {
    ensure_default_env_profiles();
    int idx = find_profile_index_by_id(id);
    if (idx < 0) return false;
    if (env_profile_count <= 1) return false;

    for (uint8_t i = idx; i + 1 < env_profile_count; ++i) {
        env_profiles[i] = env_profiles[i + 1];
    }
    env_profile_count--;
    if (active_env_profile_id == id) {
        active_env_profile_id = env_profiles[0].id;
        copy_profile_to_config(current_config, env_profiles[0]);
    }
    current_config.env_profile_count = env_profile_count;
    return save_env_profiles_to_nvs();
}

static String json_escape_cfg(const String& value) {
    String out = value;
    out.replace("\\", "\\\\");
    out.replace("\"", "\\\"");
    return out;
}

String config_env_profiles_json() {
    ensure_default_env_profiles();
    EnvProfile active;
    config_env_profile_get_active(active);
    String json = "{";
    json += "\"active_env_profile_id\":\"" + json_escape_cfg(active.id) + "\",";
    json += "\"active_env_profile_name\":\"" + json_escape_cfg(active.name) + "\",";
    json += "\"profiles\":[";
    for (uint8_t i = 0; i < env_profile_count; ++i) {
        if (i > 0) json += ",";
        const EnvProfile& p = env_profiles[i];
        json += "{\"id\":\"" + json_escape_cfg(p.id) + "\",";
        json += "\"name\":\"" + json_escape_cfg(p.name) + "\",";
        json += "\"temp_normal_min\":" + String(p.temp_normal_min, 1) + ",";
        json += "\"temp_normal_max\":" + String(p.temp_normal_max, 1) + ",";
        json += "\"temp_alarm_min\":" + String(p.temp_alarm_min, 1) + ",";
        json += "\"temp_alarm_max\":" + String(p.temp_alarm_max, 1) + ",";
        json += "\"humi_normal_min\":" + String(p.humi_normal_min, 1) + ",";
        json += "\"humi_normal_max\":" + String(p.humi_normal_max, 1) + ",";
        json += "\"humi_alarm_min\":" + String(p.humi_alarm_min, 1) + ",";
        json += "\"humi_alarm_max\":" + String(p.humi_alarm_max, 1) + "}";
    }
    json += "],\"max\":" + String(MAX_ENV_PROFILES) + "}";
    return json;
}

uint32_t config_get_effective_sample_interval_ms() {
    AppConfig cfg = config_normalize(config_get());
    uint32_t effective = cfg.sensor_sample_interval_ms;

    effective = min(effective, cfg.report_interval_ms);
    effective = min(effective, cfg.display_refresh_interval_ms);

    if (effective < DHT11_SAFE_MIN_SAMPLE_MS) {
        effective = DHT11_SAFE_MIN_SAMPLE_MS;
    }
    return effective;
}
