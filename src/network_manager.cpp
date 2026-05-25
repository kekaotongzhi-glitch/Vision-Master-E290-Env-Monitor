#include "network_manager.h"
#include "alarm_manager.h"
#include "app_state.h"
#include "config_manager.h"
#include "history_manager.h"
#include "provisioning_manager.h"
#include "ui_manager.h"
#include "web_config_server.h"
#include "wifi_profile_manager.h"
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <lvgl.h>
#include <math.h>
#include <time.h>

#define DEBUG_FORCE_BLE_PROVISIONING 0

static WiFiClient esp_client;
static PubSubClient mqtt_client(esp_client);
static WiFiManager wifi_manager;

static WifiProfile wifi_candidates[WIFI_PROFILE_MAX];
static size_t wifi_candidate_count = 0;
static size_t wifi_candidate_index = 0;
static bool wifi_connecting = false;
static uint32_t wifi_connect_started_ms = 0;
static uint32_t last_wifi_retry_ms = 0;
static uint32_t last_mqtt_retry_ms = 0;
static uint32_t last_time_sync_try_ms = 0;
static uint32_t wifi_disconnected_ms = 0;
static uint32_t last_profile_rescan_ms = 0;
static uint32_t time_sync_started_ms = 0;
static bool time_sync_started = false;
static bool time_sync_pending = false;
static bool time_sync_success_logged = false;
static uint8_t ntp_group_index = 0;
static bool wifi_was_connected = false;
static bool mqtt_was_connected = false;
static bool softap_fallback_running = false;
static bool report_now_pending = false;
static bool wifi_reconnect_requested = false;
static bool mqtt_host_empty_logged = false;

static const uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;
static const uint32_t WIFI_RETRY_INTERVAL_MS = 10000;
static const uint32_t WIFI_DISCONNECT_GRACE_MS = 5000;
static const uint32_t WIFI_PROFILE_RESCAN_INTERVAL_MS = 30000;
static const uint32_t MQTT_RETRY_INTERVAL_MS = 5000;
static const uint16_t MQTT_BUFFER_SIZE = 1024;
static const uint32_t TIME_SYNC_RETRY_INTERVAL_MS = 15000;
static const uint32_t TIME_SYNC_WAIT_MS = 10000;
static const time_t VALID_TIME_EPOCH = 1704067200;

struct NtpServerGroup {
    const char* name;
    const char* server;
};

static const NtpServerGroup NTP_SERVER_GROUPS[] = {
    {"CN", "ntp.aliyun.com"},
    {"US", "time.google.com"},
};

static const uint8_t NTP_SERVER_GROUP_COUNT = sizeof(NTP_SERVER_GROUPS) / sizeof(NTP_SERVER_GROUPS[0]);

static String mqtt_topic(const String& suffix) {
    AppConfig cfg = config_get();
    String prefix = cfg.topic_prefix.length() ? cfg.topic_prefix : "devices/" + cfg.device_id;
    if (prefix.endsWith("/")) prefix.remove(prefix.length() - 1);
    return prefix + "/" + suffix;
}

static String mqtt_base_topic(const AppConfig& cfg) {
    String prefix = cfg.topic_prefix.length() ? cfg.topic_prefix : "devices/" + cfg.device_id;
    if (prefix.endsWith("/")) prefix.remove(prefix.length() - 1);
    return prefix;
}

static String json_string_value(const String& body, const char* key) {
    String needle = String("\"") + key + "\"";
    int key_pos = body.indexOf(needle);
    if (key_pos < 0) return "";
    int colon = body.indexOf(':', key_pos + needle.length());
    if (colon < 0) return "";
    int first_quote = body.indexOf('"', colon + 1);
    if (first_quote < 0) return "";
    int second_quote = body.indexOf('"', first_quote + 1);
    if (second_quote < 0) return "";
    return body.substring(first_quote + 1, second_quote);
}

static bool json_u32_value(const String& body, const char* key, uint32_t& value) {
    String needle = String("\"") + key + "\"";
    int key_pos = body.indexOf(needle);
    if (key_pos < 0) return false;
    int colon = body.indexOf(':', key_pos + needle.length());
    if (colon < 0) return false;
    int end = colon + 1;
    while (end < body.length() && isspace(body[end])) end++;
    int start = end;
    while (end < body.length() && isdigit(body[end])) end++;
    if (end == start) return false;
    value = static_cast<uint32_t>(body.substring(start, end).toInt());
    return true;
}

static bool json_float_value(const String& body, const char* key, float& value) {
    String needle = String("\"") + key + "\"";
    int key_pos = body.indexOf(needle);
    if (key_pos < 0) return false;
    int colon = body.indexOf(':', key_pos + needle.length());
    if (colon < 0) return false;
    int end = colon + 1;
    while (end < body.length() && isspace(body[end])) end++;
    int start = end;
    while (end < body.length() && (isdigit(body[end]) || body[end] == '.' || body[end] == '-')) end++;
    if (end == start) return false;
    value = body.substring(start, end).toFloat();
    return true;
}

static String json_escape(const String& value) {
    String out = value;
    out.replace("\\", "\\\\");
    out.replace("\"", "\\\"");
    return out;
}

static bool mqtt_publish_json(const String& topic, const String& payload) {
    if (!mqtt_client.connected()) return false;
    bool ok = mqtt_client.publish(topic.c_str(), payload.c_str());
    if (!ok) {
        Serial.print("[MQTT] 发布失败：");
        Serial.println(topic);
    }
    return ok;
}

static bool looks_like_json_object(const String& payload) {
    String s = payload;
    s.trim();
    return s.startsWith("{") && s.endsWith("}");
}

static bool publish_status() {
    String payload = "{";
    payload += "\"online\":true,";
    payload += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
    payload += "\"firmware\":\"dev\",";
    payload += "\"mqtt_connected\":" + String(mqtt_client.connected() ? "true" : "false");
    payload += "}";
    return mqtt_publish_json(mqtt_topic("status"), payload);
}

static String build_telemetry_payload(float temp, float hum, int batt, float batt_voltage, const char* report_reason, bool force_stale) {
    uint32_t now = millis();
    uint32_t effective_sample_ms = config_get_effective_sample_interval_ms();
    uint32_t data_age_ms = app_state_data_age_ms(now);
    bool data_fresh = !force_stale && app_state_data_fresh(now, effective_sample_ms);
    AppRuntimeState state = app_state_get();
    EnvProfile active_profile;
    config_env_profile_get_active(active_profile);

    String payload = "{";
    payload += "\"temperature\":" + String(temp, 1) + ",";
    payload += "\"humidity\":" + String(hum, 1) + ",";
    payload += "\"battery_percent\":" + String(batt) + ",";
    payload += "\"battery_voltage\":" + String(batt_voltage, 3) + ",";
    payload += "\"rssi\":" + String(network_get_rssi()) + ",";
    payload += "\"uptime_ms\":" + String(now) + ",";
    payload += "\"report_reason\":\"" + String(report_reason ? report_reason : "interval") + "\",";
    payload += "\"data_age_ms\":";
    payload += data_age_ms == UINT32_MAX ? String("null") : String(data_age_ms);
    payload += ",";
    payload += "\"data_fresh\":" + String(data_fresh ? "true" : "false") + ",";
    payload += "\"env_level\":\"" + environment_level_to_string(state.env_level) + "\",";
    payload += "\"env_reason\":\"" + environment_reason_to_string(state.env_reason) + "\",";
    payload += "\"env_message\":\"" + json_escape(state.env_message_cn) + "\",";
    payload += "\"active_env_profile_id\":\"" + json_escape(active_profile.id) + "\",";
    payload += "\"active_env_profile_name\":\"" + json_escape(active_profile.name) + "\",";
    payload += "\"alarm_active\":" + String(state.alarm_active ? "true" : "false") + ",";
    payload += "\"warning_active\":" + String(state.warning_active ? "true" : "false") + ",";
    payload += alarm_manager_status_json();
    payload += "}";
    return payload;
}

static void publish_control_report(const String& cmd, bool ok, const String& msg) {
    String payload = "{";
    payload += "\"ok\":" + String(ok ? "true" : "false") + ",";
    payload += "\"cmd\":\"" + cmd + "\",";
    payload += "\"msg\":\"" + msg + "\"";
    payload += "}";
    if (mqtt_publish_json(mqtt_topic("control/report"), payload)) {
        Serial.println("[MQTT] 已回复 control/report");
    } else {
        Serial.println("[MQTT] control/report 发布失败");
    }
}

static void publish_control_payload(const String& payload) {
    if (mqtt_publish_json(mqtt_topic("control/report"), payload)) {
        Serial.println("[MQTT] 已回复 control/report");
    } else {
        Serial.println("[MQTT] control/report 发布失败");
    }
}

static void publish_config_report(bool ok, const String& msg) {
    AppConfig cfg = config_get();
    EnvProfile active_profile;
    config_env_profile_get_active(active_profile);
    String payload = "{";
    payload += "\"ok\":" + String(ok ? "true" : "false") + ",";
    payload += "\"msg\":\"" + msg + "\",";
    payload += "\"sensor_sample_interval_ms\":" + String(cfg.sensor_sample_interval_ms) + ",";
    payload += "\"report_interval_ms\":" + String(cfg.report_interval_ms) + ",";
    payload += "\"display_refresh_interval_ms\":" + String(cfg.display_refresh_interval_ms) + ",";
    payload += "\"effective_sample_interval_ms\":" + String(config_get_effective_sample_interval_ms()) + ",";
    payload += "\"sample_interval_ms\":" + String(cfg.sample_interval_ms) + ",";
    payload += "\"upload_interval_ms\":" + String(cfg.upload_interval_ms) + ",";
    payload += "\"battery_voltage_factor\":" + String(cfg.battery_voltage_factor, 3) + ",";
    payload += "\"environment_preset\":\"" + cfg.environment_preset + "\",";
    payload += "\"active_env_profile_id\":\"" + json_escape(active_profile.id) + "\",";
    payload += "\"active_env_profile_name\":\"" + json_escape(active_profile.name) + "\",";
    payload += "\"env_profile_count\":" + String(cfg.env_profile_count) + ",";
    payload += "\"temp_normal_min\":" + String(cfg.temp_normal_min, 1) + ",";
    payload += "\"temp_normal_max\":" + String(cfg.temp_normal_max, 1) + ",";
    payload += "\"temp_alarm_min\":" + String(cfg.temp_alarm_min, 1) + ",";
    payload += "\"temp_alarm_max\":" + String(cfg.temp_alarm_max, 1) + ",";
    payload += "\"humi_normal_min\":" + String(cfg.humi_normal_min, 1) + ",";
    payload += "\"humi_normal_max\":" + String(cfg.humi_normal_max, 1) + ",";
    payload += "\"humi_alarm_min\":" + String(cfg.humi_alarm_min, 1) + ",";
    payload += "\"humi_alarm_max\":" + String(cfg.humi_alarm_max, 1);
    payload += "}";
    if (mqtt_publish_json(mqtt_topic("config/report"), payload)) {
        Serial.println("[MQTT] 已回复 config/report");
    } else {
        Serial.println("[MQTT] config/report 发布失败");
    }
}

static void handle_mqtt_control_set(const String& payload) {
    Serial.println("[MQTT] 收到 control/set");
    if (!looks_like_json_object(payload)) {
        publish_control_report("unknown", false, "invalid json");
        return;
    }
    String cmd = json_string_value(payload, "cmd");
    if (cmd == "ping") {
        publish_control_report(cmd, true, "pong");
    } else if (cmd == "report_status") {
        publish_status();
        publish_control_report(cmd, true, "status reported");
    } else if (cmd == "report_now") {
        Serial.println("[MQTT] 已请求立即上报");
        report_now_pending = true;
    } else if (cmd == "report_alarms") {
        String report = alarm_manager_events_json();
        if (report.startsWith("{")) {
            report = report.substring(1);
        }
        String response = "{\"ok\":true,\"cmd\":\"report_alarms\",";
        response += report;
        publish_control_payload(response);
    } else if (cmd == "ack_alarms") {
        alarm_manager_ack_all();
        publish_control_report(cmd, true, "alarms acknowledged");
    } else if (cmd == "ack_alarm") {
        uint32_t id = 0;
        json_u32_value(payload, "id", id);
        bool ok = id > 0 && alarm_manager_ack(id);
        String response = "{";
        response += "\"ok\":" + String(ok ? "true" : "false") + ",";
        response += "\"cmd\":\"ack_alarm\",";
        response += "\"id\":" + String(id) + ",";
        response += "\"msg\":\"" + String(ok ? "alarm acknowledged" : "alarm not found") + "\"";
        response += "}";
        publish_control_payload(response);
    } else if (cmd == "report_history") {
        uint32_t limit = 20;
        json_u32_value(payload, "limit", limit);
        if (limit == 0 || limit > 50) limit = 50;
        String report = history_manager_json(limit);
        if (report.startsWith("{")) {
            report = report.substring(1);
        }
        String response = "{\"ok\":true,\"cmd\":\"report_history\",";
        response += report;
        publish_control_payload(response);
    } else {
        publish_control_report(cmd.length() ? cmd : "unknown", false, "unsupported command");
    }
}

static void handle_mqtt_config_set(const String& payload) {
    Serial.println("[MQTT] 收到 config/set");
    if (!looks_like_json_object(payload)) {
        publish_config_report(false, "invalid json");
        return;
    }
    AppConfig cfg = config_get();
    uint32_t u32_value = 0;
    float float_value = 0.0f;
    bool changed = false;
    bool thresholds_changed = false;

    String profile_action = json_string_value(payload, "env_profile_action");
    if (profile_action == "delete") {
        String profile_id = json_string_value(payload, "profile_id");
        if (!config_env_profile_delete(profile_id)) {
            publish_config_report(false, "env profile delete failed");
            return;
        }
        publish_config_report(true, "env profile deleted");
        return;
    }
    if (profile_action == "save") {
        EnvProfile profile;
        profile.id = json_string_value(payload, "id");
        if (profile.id.length() == 0) profile.id = json_string_value(payload, "profile_id");
        profile.name = json_string_value(payload, "name");
        if (!json_float_value(payload, "temp_normal_min", profile.temp_normal_min) ||
            !json_float_value(payload, "temp_normal_max", profile.temp_normal_max) ||
            !json_float_value(payload, "temp_alarm_min", profile.temp_alarm_min) ||
            !json_float_value(payload, "temp_alarm_max", profile.temp_alarm_max) ||
            !json_float_value(payload, "humi_normal_min", profile.humi_normal_min) ||
            !json_float_value(payload, "humi_normal_max", profile.humi_normal_max) ||
            !json_float_value(payload, "humi_alarm_min", profile.humi_alarm_min) ||
            !json_float_value(payload, "humi_alarm_max", profile.humi_alarm_max)) {
            publish_config_report(false, "missing env profile threshold");
            return;
        }
        EnvProfile saved;
        if (!config_env_profile_save(profile, &saved)) {
            publish_config_report(false, "env profile save failed");
            return;
        }
        publish_config_report(true, "env profile saved");
        return;
    }

    String active_profile_id = json_string_value(payload, "active_env_profile_id");
    if (active_profile_id.length()) {
        if (!config_env_profile_select(active_profile_id)) {
            publish_config_report(false, "env profile not found");
            return;
        }
        publish_config_report(true, "env profile selected");
        return;
    }

    String preset = json_string_value(payload, "environment_preset");
    if (preset.length()) {
        if (preset == "home" || preset == "farm") {
            if (!config_env_profile_select(preset)) {
                publish_config_report(false, "env profile not found");
                return;
            }
            publish_config_report(true, "env profile selected");
            return;
        }
        if (preset != "custom") {
            publish_config_report(false, "invalid environment_preset");
            return;
        }
        changed = true;
    }

    if (json_u32_value(payload, "sample_interval_ms", u32_value) || json_u32_value(payload, "sensor_sample_interval_ms", u32_value)) {
        if (u32_value > 3600000UL) {
            publish_config_report(false, "invalid sample_interval_ms");
            return;
        }
        cfg.sensor_sample_interval_ms = u32_value;
        cfg.sample_interval_ms = u32_value;
        changed = true;
    }

    if (json_u32_value(payload, "upload_interval_ms", u32_value) || json_u32_value(payload, "report_interval_ms", u32_value)) {
        if (u32_value > 3600000UL) {
            publish_config_report(false, "invalid upload_interval_ms");
            return;
        }
        cfg.report_interval_ms = u32_value;
        cfg.upload_interval_ms = u32_value;
        changed = true;
    }

    if (json_u32_value(payload, "display_refresh_interval_ms", u32_value)) {
        if (u32_value > 3600000UL) {
            publish_config_report(false, "invalid display_refresh_interval_ms");
            return;
        }
        cfg.display_refresh_interval_ms = u32_value;
        changed = true;
    }

    if (json_float_value(payload, "battery_voltage_factor", float_value)) {
        if (float_value < 1.0f || float_value > 20.0f) {
            publish_config_report(false, "invalid battery_voltage_factor");
            return;
        }
        cfg.battery_voltage_factor = float_value;
        changed = true;
    }

    if (json_float_value(payload, "temp_normal_min", float_value)) { cfg.temp_normal_min = float_value; changed = true; thresholds_changed = true; }
    if (json_float_value(payload, "temp_normal_max", float_value)) { cfg.temp_normal_max = float_value; changed = true; thresholds_changed = true; }
    if (json_float_value(payload, "temp_alarm_min", float_value)) { cfg.temp_alarm_min = float_value; changed = true; thresholds_changed = true; }
    if (json_float_value(payload, "temp_alarm_max", float_value)) { cfg.temp_alarm_max = float_value; changed = true; thresholds_changed = true; }
    if (json_float_value(payload, "humi_normal_min", float_value)) { cfg.humi_normal_min = float_value; changed = true; thresholds_changed = true; }
    if (json_float_value(payload, "humi_normal_max", float_value)) { cfg.humi_normal_max = float_value; changed = true; thresholds_changed = true; }
    if (json_float_value(payload, "humi_alarm_min", float_value)) { cfg.humi_alarm_min = float_value; changed = true; thresholds_changed = true; }
    if (json_float_value(payload, "humi_alarm_max", float_value)) { cfg.humi_alarm_max = float_value; changed = true; thresholds_changed = true; }
    if (thresholds_changed) {
        if (!config_validate_thresholds(cfg)) {
            publish_config_report(false, "invalid threshold order");
            return;
        }
    }

    if (!changed) {
        publish_config_report(false, "no supported config field");
        return;
    }

    if (!config_save(cfg)) {
        publish_config_report(false, "config save failed");
        return;
    }

    publish_config_report(true, "config saved");
}

static void mqtt_callback(char* topic, uint8_t* payload, unsigned int length) {
    String message;
    message.reserve(length);
    for (unsigned int i = 0; i < length; ++i) {
        message += static_cast<char>(payload[i]);
    }

    Serial.print("[MQTT] 收到消息，topic=");
    Serial.print(topic);
    Serial.print(" bytes=");
    Serial.println(length);

    String topic_str(topic);
    AppConfig cb_cfg = config_get();
    String base_topic = mqtt_base_topic(cb_cfg);
    if (topic_str == base_topic + "/config/set") {
        handle_mqtt_config_set(message);
    } else if (topic_str == base_topic + "/control/set") {
        handle_mqtt_control_set(message);
    }
}

static void ensure_china_timezone() {
    static bool timezone_set = false;
    if (timezone_set) return;
    setenv("TZ", "CST-8", 1);
    tzset();
    timezone_set = true;
}

static void request_ntp_time_sync() {
    const NtpServerGroup& group = NTP_SERVER_GROUPS[ntp_group_index % NTP_SERVER_GROUP_COUNT];
    configTime(0, 0, group.server);
    Serial.print("[NTP] 请求校时，分组=");
    Serial.print(group.name);
    Serial.print(" 服务器=");
    Serial.println(group.server);
}

static bool build_wifi_candidates() {
    wifi_candidate_count = wifi_profiles_select_available(wifi_candidates, WIFI_PROFILE_MAX);
    wifi_candidate_index = 0;

    Serial.print("[WiFi] 可用已保存网络数量=");
    Serial.println(wifi_candidate_count);
    return wifi_candidate_count > 0;
}

static void connect_mqtt_if_needed() {
    if (WiFi.status() != WL_CONNECTED) {
        if (mqtt_client.connected()) mqtt_client.disconnect();
        ui_set_mqtt_status(MqttStatus::DISCONNECTED);
        return;
    }

    if (mqtt_client.connected()) {
        ui_set_mqtt_status(MqttStatus::CONNECTED);
        return;
    }

    uint32_t now = millis();
    if (now - last_mqtt_retry_ms < MQTT_RETRY_INTERVAL_MS) return;
    last_mqtt_retry_ms = now;

    AppConfig cfg = config_get();
    if (cfg.mqtt_host.length() == 0) {
        ui_set_mqtt_status(MqttStatus::DISCONNECTED);
        if (!mqtt_host_empty_logged) {
            Serial.println("[MQTT] 未配置服务器，跳过连接");
            mqtt_host_empty_logged = true;
        }
        return;
    }
    mqtt_host_empty_logged = false;

    mqtt_client.setServer(cfg.mqtt_host.c_str(), cfg.mqtt_port);
    mqtt_client.setBufferSize(MQTT_BUFFER_SIZE);
    mqtt_client.setCallback(mqtt_callback);
    ui_set_mqtt_status(MqttStatus::CONNECTING);
    Serial.print("[MQTT] 正在连接：");
    Serial.print(cfg.mqtt_host);
    Serial.print(":");
    Serial.println(cfg.mqtt_port);
    String client_id = cfg.mqtt_client_id.length() ? cfg.mqtt_client_id : cfg.device_id;
    Serial.print("[MQTT] client_id=");
    Serial.println(client_id);

    bool connected = false;
    if (cfg.mqtt_username.length() > 0) {
        connected = mqtt_client.connect(client_id.c_str(), cfg.mqtt_username.c_str(), cfg.mqtt_password.c_str());
    } else {
        connected = mqtt_client.connect(client_id.c_str());
    }

    if (connected) {
        Serial.println("[MQTT] 已连接");
        mqtt_was_connected = true;
        String base_topic = mqtt_base_topic(cfg);
        Serial.print("[MQTT] topic_prefix=");
        Serial.println(base_topic);
        String config_topic = base_topic + "/config/set";
        String control_topic = base_topic + "/control/set";
        bool config_sub_ok = mqtt_client.subscribe(config_topic.c_str(), 0);
        bool control_sub_ok = mqtt_client.subscribe(control_topic.c_str(), 0);
        Serial.printf("[MQTT] 订阅 %s：%s\n", config_topic.c_str(), config_sub_ok ? "成功" : "失败");
        Serial.printf("[MQTT] 订阅 %s：%s\n", control_topic.c_str(), control_sub_ok ? "成功" : "失败");
        if (!config_sub_ok || !control_sub_ok) {
            Serial.printf("[MQTT] 订阅失败，state=%d\n", mqtt_client.state());
        }
        ui_set_mqtt_status(MqttStatus::CONNECTED);
        publish_status();
    } else {
        Serial.print("[MQTT] 连接失败，state=");
        Serial.println(mqtt_client.state());
        ui_set_mqtt_status(MqttStatus::DISCONNECTED);
    }
}

static void start_wifi_connect() {
    if (wifi_connecting) return;

    if (wifi_candidate_index >= wifi_candidate_count && !build_wifi_candidates()) {
        if (wifi_profiles_count() == 0) {
            Serial.println("[WiFi] 没有已保存网络");
            ui_set_wifi_status(WifiStatus::UNCONFIGURED);
            if (!provisioning_is_running()) {
                provisioning_start_ble();
            }
        } else {
            Serial.println("[WiFi] 当前没有可用的已保存网络");
            ui_set_wifi_status(WifiStatus::OFFLINE);
        }
        return;
    }

    WifiProfile& profile = wifi_candidates[wifi_candidate_index];
    Serial.print("[WiFi] 正在连接 SSID=");
    Serial.print(profile.ssid);
    Serial.print(" priority=");
    Serial.print(profile.priority);
    Serial.print(" rssi=");
    Serial.println(profile.rssi);

    WiFi.mode(WIFI_STA);
    WiFi.setHostname("eink-sensor");
    WiFi.begin(profile.ssid.c_str(), profile.password.c_str());
    wifi_connecting = true;
    wifi_connect_started_ms = millis();
    ui_set_wifi_status(WifiStatus::CONNECTING);
}

bool network_wifi_profile_save(const String& ssid, const String& password, bool password_provided, int priority, bool enabled) {
    return wifi_profiles_save_profile(ssid, password, password_provided, priority, enabled);
}

bool network_wifi_profile_delete(const String& ssid) {
    return wifi_profiles_remove(ssid);
}

String network_wifi_profiles_json() {
    WifiProfile profiles[WIFI_PROFILE_MAX];
    size_t count = wifi_profiles_get_all(profiles, WIFI_PROFILE_MAX);
    bool connected = WiFi.status() == WL_CONNECTED;
    String current_ssid = connected ? WiFi.SSID() : "";

    String json = "{";
    json += "\"connected\":" + String(connected ? "true" : "false") + ",";
    json += "\"current_ssid\":\"" + json_escape(current_ssid) + "\",";
    json += "\"ip\":\"" + (connected ? WiFi.localIP().toString() : String("")) + "\",";
    json += "\"rssi\":";
    json += connected ? String(WiFi.RSSI()) : String("null");
    json += ",\"max\":" + String(WIFI_PROFILE_MAX) + ",";
    json += "\"profiles\":[";
    for (size_t i = 0; i < count; ++i) {
        if (i > 0) json += ",";
        json += "{\"ssid\":\"" + json_escape(profiles[i].ssid) + "\",";
        json += "\"priority\":" + String(profiles[i].priority) + ",";
        json += "\"enabled\":" + String(profiles[i].enabled ? "true" : "false") + ",";
        json += "\"current\":" + String(connected && profiles[i].ssid == current_ssid ? "true" : "false") + ",";
        json += "\"has_password\":" + String(profiles[i].password.length() > 0 ? "true" : "false") + "}";
    }
    json += "]}";
    return json;
}

void network_request_wifi_reconnect() {
    wifi_reconnect_requested = true;
}

bool network_init() {
    ensure_china_timezone();
    WiFi.mode(WIFI_STA);
    mqtt_client.setSocketTimeout(1);
    wifi_profiles_init();

#if DEBUG_FORCE_BLE_PROVISIONING
    Serial.println("[调试] 已启用强制 BLE 配网");
    ui_set_wifi_status(WifiStatus::UNCONFIGURED);
    provisioning_start_ble();
    return false;
#endif

    start_wifi_connect();

    if (WiFi.status() == WL_CONNECTED) {
        ui_set_wifi_status(WifiStatus::CONNECTED);
        web_config_begin();
        network_sync_time();
        connect_mqtt_if_needed();
        return true;
    }
    return false;
}

void network_process() {
    uint32_t now = millis();

#if DEBUG_FORCE_BLE_PROVISIONING
    if (!provisioning_is_running()) {
        provisioning_start_ble();
    }
    return;
#endif

    if (softap_fallback_running) {
        wifi_manager.process();
    }

    if (wifi_reconnect_requested) {
        wifi_reconnect_requested = false;
        Serial.println("[WiFi] 已请求重新连接");
        wifi_connecting = false;
        wifi_candidate_count = 0;
        wifi_candidate_index = 0;
        wifi_was_connected = false;
        last_wifi_retry_ms = 0;
        if (mqtt_client.connected()) mqtt_client.disconnect();
        WiFi.disconnect(false, false);
        start_wifi_connect();
        return;
    }

    if (provisioning_is_running() && WiFi.status() != WL_CONNECTED) {
        wifi_connecting = false;
        ui_set_wifi_status(WifiStatus::UNCONFIGURED);
        if (wifi_profiles_count() > 0 && now - last_profile_rescan_ms >= WIFI_PROFILE_RESCAN_INTERVAL_MS) {
            last_profile_rescan_ms = now;
            Serial.println("[WiFi] 配网待机中，重新检查已保存网络");
            start_wifi_connect();
        }
        return;
    }

    if (WiFi.status() == WL_CONNECTED) {
        if (wifi_connecting) {
            Serial.print("[WiFi] 已连接，IP=");
            Serial.println(WiFi.localIP());
        }
        if (!wifi_was_connected) {
            wifi_was_connected = true;
            wifi_disconnected_ms = 0;
            time_sync_started = false;
            time_sync_pending = false;
            time_sync_success_logged = false;
            ntp_group_index = 0;
            Serial.print("[WiFi] 网络就绪，IP=");
            Serial.println(WiFi.localIP());
        }
        wifi_connecting = false;
        ui_set_wifi_status(WifiStatus::CONNECTED);
        if (!web_config_is_running()) {
            web_config_begin();
        }

        connect_mqtt_if_needed();
        if (mqtt_client.connected()) {
            bool loop_ok = mqtt_client.loop();
            if (!loop_ok) {
                Serial.printf("[MQTT] loop 失败，state=%d\n", mqtt_client.state());
            }
        } else if (mqtt_was_connected) {
            mqtt_was_connected = false;
            Serial.printf("[MQTT] 已断开，state=%d\n", mqtt_client.state());
        }

        if (time_sync_pending || !time_sync_started || now - last_time_sync_try_ms >= TIME_SYNC_RETRY_INTERVAL_MS) {
            network_sync_time();
        }
        return;
    }

    wifi_was_connected = false;
    if (wifi_disconnected_ms == 0) {
        wifi_disconnected_ms = now;
    }

    if (mqtt_client.connected()) {
        mqtt_client.disconnect();
    } else if (mqtt_was_connected) {
        Serial.printf("[MQTT] 已断开，state=%d\n", mqtt_client.state());
    }
    mqtt_was_connected = false;
    web_config_stop();
    ui_set_mqtt_status(MqttStatus::DISCONNECTED);

    if (wifi_connecting && now - wifi_connect_started_ms >= WIFI_CONNECT_TIMEOUT_MS) {
        wifi_connecting = false;
        Serial.println("[WiFi] 连接超时");
        wifi_candidate_index++;
        if (wifi_candidate_index < wifi_candidate_count) {
            Serial.println("[WiFi] 尝试下一个已保存网络");
            start_wifi_connect();
        } else {
            ui_set_wifi_status(WifiStatus::OFFLINE);
            Serial.println("[WiFi] 所有已保存网络连接失败，启动 BLE 配网");
            provisioning_start_ble();
        }
    }

    if (!wifi_connecting && now - last_wifi_retry_ms >= WIFI_RETRY_INTERVAL_MS) {
        last_wifi_retry_ms = now;
        if (now - wifi_disconnected_ms < WIFI_DISCONNECT_GRACE_MS) {
            return;
        }
        start_wifi_connect();
    }
}

void network_sync_time() {
    ensure_china_timezone();
    time_t now = time(nullptr);
    if (now >= VALID_TIME_EPOCH && time_sync_started) {
        time_sync_started = true;
        time_sync_pending = false;
        if (!time_sync_success_logged) {
            struct tm timeinfo;
            localtime_r(&now, &timeinfo);
            char buf[24];
            strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &timeinfo);
            Serial.print("[NTP] 校时成功：");
            Serial.println(buf);
            time_sync_success_logged = true;
        }
        return;
    }

    if (WiFi.status() != WL_CONNECTED) {
        time_sync_pending = false;
        return;
    }

    uint32_t ms = millis();

    if (!time_sync_pending) {
        last_time_sync_try_ms = ms;
        time_sync_started_ms = ms;
        time_sync_started = true;
        time_sync_pending = true;

        request_ntp_time_sync();
        return;
    }

    if (ms - time_sync_started_ms < TIME_SYNC_WAIT_MS) {
        return;
    }

    time_sync_pending = false;
    now = time(nullptr);

    if (now >= VALID_TIME_EPOCH) {
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        char buf[24];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &timeinfo);
        Serial.print("[NTP] 校时成功：");
        Serial.println(buf);
        time_sync_success_logged = true;
        return;
    }

    ntp_group_index = (ntp_group_index + 1) % NTP_SERVER_GROUP_COUNT;
}

String network_get_time_str() {
    ensure_china_timezone();
    time_t now = time(nullptr);
    if (now < VALID_TIME_EPOCH) return String("--:--");
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    char buf[10];
    strftime(buf, sizeof(buf), "%H:%M", &timeinfo);
    return String(buf);
}

int network_get_rssi() {
    if (WiFi.status() != WL_CONNECTED) return 0;
    return WiFi.RSSI();
}

bool network_is_wifi_connected() {
    return WiFi.status() == WL_CONNECTED;
}

bool network_is_mqtt_connected() {
    return mqtt_client.connected();
}

bool network_report(float temp, float hum, int batt, float batt_voltage, const char* report_reason, bool force_stale) {
    if (isnan(temp) || isnan(hum)) {
        Serial.println("[MQTT] 跳过上报：传感器数据无效");
        return false;
    }
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[MQTT] 跳过上报：Wi-Fi 离线");
        return false;
    }

    connect_mqtt_if_needed();
    if (!mqtt_client.connected()) {
        Serial.println("[MQTT] 跳过上报：MQTT 离线");
        return false;
    }

    String payload = build_telemetry_payload(temp, hum, batt, batt_voltage, report_reason, force_stale);
    bool ok = mqtt_publish_json(mqtt_topic("telemetry"), payload);
    ok &= publish_status();

    mqtt_client.loop();

    if (!ok) {
        Serial.print("[MQTT] 上报失败，state=");
        Serial.println(mqtt_client.state());
    }
    return ok;
}

bool network_report_cached(const char* report_reason, bool force_stale) {
    AppRuntimeState state = app_state_get();
    return network_report(state.temperature, state.humidity, state.battery_percent, state.battery_voltage, report_reason, force_stale);
}

bool network_report_now_requested() {
    return report_now_pending;
}

void network_clear_report_now_request() {
    report_now_pending = false;
}

void network_finish_report_now(bool telemetry_ok) {
    publish_control_report("report_now", telemetry_ok, telemetry_ok ? "telemetry reported" : "telemetry failed");
}

void start_softap_fallback_portal() {
#ifdef ENABLE_SOFTAP_FALLBACK
    if (softap_fallback_running) return;
    Serial.println("[WiFi] 启动 SoftAP 兜底入口");
    wifi_manager.setConfigPortalBlocking(false);
    wifi_manager.setConfigPortalTimeout(180);
    wifi_manager.startConfigPortal("BEAVER_SENSOR_FALLBACK");
    softap_fallback_running = true;
#else
    Serial.println("[WiFi] SoftAP 兜底入口未启用");
#endif
}
