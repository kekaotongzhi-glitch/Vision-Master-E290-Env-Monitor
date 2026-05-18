#include "web_config_server.h"
#include "alarm_manager.h"
#include "app_state.h"
#include "config_manager.h"
#include "history_manager.h"
#include "network_manager.h"
#include "wifi_profile_manager.h"
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <math.h>
#include <string.h>

static WebServer server(80);
static bool server_running = false;
static bool server_starting = false;
static bool mdns_running = false;
static String web_config_url;
static String web_config_ip_url;
static String web_config_mdns_url;
static bool restart_pending = false;
static uint32_t restart_at_ms = 0;

static const char* HTML_CONTENT_TYPE = "text/html; charset=utf-8";
static const char* JSON_CONTENT_TYPE = "application/json; charset=utf-8";

static String normalized_mdns_name() {
    AppConfig cfg = config_get();
    String mdns_name = cfg.device_name.length() ? cfg.device_name : "eink-sensor";
    mdns_name.replace(" ", "-");
    mdns_name.toLowerCase();
    return mdns_name;
}

static String html_escape(const String& value) {
    String out = value;
    out.replace("&", "&amp;");
    out.replace("\"", "&quot;");
    out.replace("<", "&lt;");
    out.replace(">", "&gt;");
    return out;
}

static String json_escape(const String& value) {
    String out = value;
    out.replace("\\", "\\\\");
    out.replace("\"", "\\\"");
    return out;
}

static String page_shell(const String& title, const String& body) {
    String html = "<!doctype html><html><head><meta charset=\"UTF-8\">";
    html += "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">";
    html += "<title>" + html_escape(title) + "</title>";
    html += "<style>";
    html += "body{font-family:Arial,'Microsoft YaHei',sans-serif;max-width:760px;margin:24px auto;padding:0 16px;line-height:1.5;color:#111;background:#fafafa}";
    html += "header{display:flex;justify-content:space-between;align-items:center;gap:12px}";
    html += "section{background:#fff;border:1px solid #e6e6e6;border-radius:8px;padding:16px;margin:14px 0}";
    html += "h1{font-size:24px;margin:0}h2{font-size:18px;margin:0 0 12px}";
    html += "label{display:block;margin-top:10px;font-weight:600}";
    html += "input{box-sizing:border-box;width:100%;padding:8px;margin-top:4px}";
    html += "button,.button{display:inline-block;margin-top:14px;padding:8px 14px;border:1px solid #111;border-radius:6px;background:#fff;color:#111;text-decoration:none;cursor:pointer}";
    html += "button.primary{background:#111;color:#fff}.muted{color:#555;font-size:14px}.actions{display:flex;flex-wrap:wrap;gap:10px;align-items:center}";
    html += ".status-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(210px,1fr));gap:10px}.status-row{border:1px solid #eee;border-radius:6px;padding:10px;background:#fcfcfc}.status-row span:first-child{display:block;color:#666;font-size:13px}.value{display:block;font-weight:700;font-size:18px;margin-top:4px}";
    html += ".env-normal{border-color:#ddd}.env-warning{border-color:#b88700;background:#fff8e1}.env-alarm{border-color:#b00020;background:#ffe8ec}";
    html += "details{border-top:1px solid #eee;margin-top:16px;padding-top:12px}summary{cursor:pointer;font-weight:700}.help{margin:8px 0 0;padding-left:20px}.danger{color:#777;border-color:#ccc}";
    html += ".hms{display:grid;grid-template-columns:1fr auto 1fr auto 1fr auto;gap:6px;align-items:center}.hms input{margin-top:0}";
    html += ".env-profile-panel{padding:18px}.profile-topbar{display:grid;grid-template-columns:minmax(220px,1fr) auto auto;gap:12px;align-items:end;margin-top:12px}.profile-topbar select{box-sizing:border-box;width:100%;padding:8px;margin-top:4px}.profile-topbar button{white-space:nowrap}";
    html += ".profile-edit-card{border:1px solid #e3e3e3;border-radius:8px;background:#fcfcfc;padding:14px;margin-top:14px}.edit-header{display:flex;justify-content:space-between;align-items:center;gap:10px;flex-wrap:wrap}.edit-header h3{margin:0;font-size:16px}.badge{font-size:12px;border:1px solid #ddd;border-radius:999px;padding:3px 8px;background:#fff;color:#555}";
    html += ".threshold-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(420px,1fr));gap:16px;margin-top:14px}.threshold-card{border:1px solid #e0e0e0;border-radius:14px;background:#fff;padding:18px 20px}.threshold-card h4{margin:0 0 14px;font-size:18px}.threshold-table{display:grid;gap:10px}.threshold-head,.threshold-line{display:grid;grid-template-columns:120px 1fr 1fr 48px;align-items:center;column-gap:14px}.threshold-head{color:#666;font-size:14px;padding-bottom:4px;border-bottom:1px solid #eee}.threshold-label{font-weight:600;white-space:nowrap}.threshold-cell.value-only{display:grid;grid-template-columns:minmax(90px,140px);align-items:center}.threshold-cell.with-op{display:grid;grid-template-columns:28px minmax(90px,140px);align-items:center;column-gap:8px}.threshold-cell input{width:100%;box-sizing:border-box;margin-top:0}.op{text-align:center;color:#444;white-space:nowrap}.threshold-unit{white-space:nowrap;color:#111}.profile-actions{display:flex;gap:12px;flex-wrap:wrap;align-items:center;margin-top:16px}.env-profile-panel #env_profile_notice{display:block;margin-top:10px}";
    html += ".alarm-table{width:100%;border-collapse:collapse;margin-top:10px;font-size:14px}.alarm-table th,.alarm-table td{border-bottom:1px solid #eee;padding:8px 6px;text-align:left}.alarm-table th{color:#666;font-weight:600}.alarm-actions{display:flex;gap:10px;flex-wrap:wrap;align-items:center;margin-top:10px}.alarm-active{color:#b00020;font-weight:700}.alarm-ok{color:#555}";
    html += ".history-chart-card{border:1px solid #e6e6e6;border-radius:8px;background:#fcfcfc;padding:12px;margin-top:10px}.chart-toolbar{display:flex;gap:12px;flex-wrap:wrap;align-items:center;margin-bottom:8px}.chart-wrap{position:relative;width:100%;height:300px}#history_chart{display:block;width:100%;height:300px;background:#fff;border:1px solid #eee;border-radius:6px}.chart-legend{display:flex;gap:16px;flex-wrap:wrap;margin-top:8px;font-size:14px}.legend-temp{color:#d35400}.legend-humi{color:#1f77b4}.chart-tooltip{position:absolute;pointer-events:none;background:rgba(255,255,255,0.96);border:1px solid #ddd;border-radius:8px;padding:8px 10px;font-size:13px;box-shadow:0 4px 12px rgba(0,0,0,0.12);display:none;z-index:10;max-width:220px}";
    html += ".history-table{width:100%;border-collapse:collapse;margin-top:10px;font-size:14px}.history-table th,.history-table td{border-bottom:1px solid #eee;padding:8px 6px;text-align:left}.history-table th{color:#666;font-weight:600}.history-actions{display:flex;gap:10px;flex-wrap:wrap;align-items:center;margin-top:10px}";
    html += ".wifi-table{width:100%;border-collapse:collapse;margin-top:10px;font-size:14px}.wifi-table th,.wifi-table td{border-bottom:1px solid #eee;padding:8px 6px;text-align:left}.wifi-table th{color:#666;font-weight:600}.wifi-form-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:10px;align-items:end}.wifi-form-grid label{margin-top:0}.wifi-form-grid input[type=checkbox]{width:auto}.wifi-actions{display:flex;gap:10px;flex-wrap:wrap;align-items:center;margin-top:10px}";
    html += "@media(max-width:760px){.threshold-grid{grid-template-columns:1fr}.profile-topbar{grid-template-columns:1fr}.profile-topbar button{width:100%}.threshold-head{display:none}.threshold-line{grid-template-columns:1fr;gap:6px;padding:10px 0;border-bottom:1px solid #eee}.threshold-cell.value-only{grid-template-columns:minmax(120px,1fr)}.threshold-cell.with-op{grid-template-columns:32px minmax(120px,1fr)}}";
    html += "</style></head><body>";
    html += body;
    html += "</body></html>";
    return html;
}

static String language_toggle_script() {
    String script = "<script>";
    script += "function setLang(lang){document.querySelectorAll('[data-zh]').forEach(function(el){el.innerHTML=el.getAttribute('data-'+lang);});";
    script += "var btn=document.getElementById('langBtn');if(btn){btn.textContent=lang==='zh'?'English':'\u4e2d\u6587';}localStorage.setItem('lang',lang);}";
    script += "var btn=document.getElementById('langBtn');if(btn){btn.onclick=function(){setLang((localStorage.getItem('lang')||'zh')==='zh'?'en':'zh');};}";
    script += "setLang(localStorage.getItem('lang')||'zh');";
    script += "</script>";
    return script;
}

static uint32_t interval_hours(uint32_t ms) {
    return ms / 3600000UL;
}

static uint32_t interval_minutes(uint32_t ms) {
    return (ms % 3600000UL) / 60000UL;
}

static uint32_t interval_seconds(uint32_t ms) {
    return (ms % 60000UL) / 1000UL;
}


static String saved_interval_summary(const AppConfig& cfg) {
    String html;
    html += "<ul>";
    html += "<li><span data-zh=\"&#x91C7;&#x6837;&#x95F4;&#x9694;\" data-en=\"Sensor sample interval\">&#x91C7;&#x6837;&#x95F4;&#x9694;</span>: " + String(cfg.sensor_sample_interval_ms / 1000UL) + " s</li>";
    html += "<li><span data-zh=\"&#x4E0A;&#x62A5;&#x95F4;&#x9694;\" data-en=\"Report interval\">&#x4E0A;&#x62A5;&#x95F4;&#x9694;</span>: " + String(cfg.report_interval_ms / 1000UL) + " s</li>";
    html += "<li><span data-zh=\"&#x5C4F;&#x5E55;&#x5237;&#x65B0;&#x95F4;&#x9694;\" data-en=\"Display refresh interval\">&#x5C4F;&#x5E55;&#x5237;&#x65B0;&#x95F4;&#x9694;</span>: " + String(cfg.display_refresh_interval_ms / 1000UL) + " s</li>";
    html += "</ul>";
    return html;
}

static String save_result_page(bool ok, const String& reason = "", const AppConfig* saved_cfg = nullptr) {
    String zh_title = ok ? "&#x914D;&#x7F6E;&#x5DF2;&#x4FDD;&#x5B58;" : "&#x4FDD;&#x5B58;&#x5931;&#x8D25;";
    String en_title = ok ? "Configuration saved." : "Save failed.";
    String body;
    body += "<header><h1 data-zh=\"" + zh_title + "\" data-en=\"" + en_title + "\">" + zh_title + "</h1>";
    body += "<button type=\"button\" id=\"langBtn\">English</button></header>";
    if (ok) {
        body += "<p data-zh=\"MQTT &#x914D;&#x7F6E;&#x5DF2;&#x4FDD;&#x5B58;&#xFF0C;&#x8BBE;&#x5907;&#x5C06;&#x5728; 2 &#x79D2;&#x540E;&#x91CD;&#x542F;&#x3002;\" data-en=\"The MQTT configuration has been saved. The device will restart in 2 seconds.\">MQTT &#x914D;&#x7F6E;&#x5DF2;&#x4FDD;&#x5B58;&#xFF0C;&#x8BBE;&#x5907;&#x5C06;&#x5728; 2 &#x79D2;&#x540E;&#x91CD;&#x542F;&#x3002;</p>";
        if (saved_cfg) body += saved_interval_summary(*saved_cfg);
        body += "<p data-zh=\"&#x91CD;&#x542F;&#x671F;&#x95F4;&#x9875;&#x9762;&#x53EF;&#x80FD;&#x6682;&#x65F6;&#x65E0;&#x6CD5;&#x8BBF;&#x95EE;&#x3002;\" data-en=\"The page may be temporarily unavailable during restart.\">&#x91CD;&#x542F;&#x671F;&#x95F4;&#x9875;&#x9762;&#x53EF;&#x80FD;&#x6682;&#x65F6;&#x65E0;&#x6CD5;&#x8BBF;&#x95EE;&#x3002;</p>";
        body += "<p data-zh=\"&#x8BF7;&#x7B49;&#x5F85; 5~8 &#x79D2;&#x540E;&#x8FD4;&#x56DE;&#x9996;&#x9875;&#x3002;\" data-en=\"Please wait 5-8 seconds, then return to the home page.\">&#x8BF7;&#x7B49;&#x5F85; 5~8 &#x79D2;&#x540E;&#x8FD4;&#x56DE;&#x9996;&#x9875;&#x3002;</p>";
        body += "<script>setTimeout(function(){window.location.href='/';},8000);</script>";
    } else {
        body += "<p><span data-zh=\"&#x4FDD;&#x5B58;&#x5931;&#x8D25;\" data-en=\"Save failed\">&#x4FDD;&#x5B58;&#x5931;&#x8D25;</span>: " + html_escape(reason) + "</p>";
    }
    body += "<a class=\"button\" href=\"/\" data-zh=\"&#x8FD4;&#x56DE;&#x9996;&#x9875;\" data-en=\"Back to Home\">&#x8FD4;&#x56DE;&#x9996;&#x9875;</a>";
    body += language_toggle_script();
    return page_shell(ok ? "Configuration saved" : "Save failed", body);
}

static String json_float_or_null(bool valid, float value, int decimals) {
    if (!valid || isnan(value)) return "null";
    return String(value, decimals);
}

static uint32_t interval_ms_from_hms_form(const char* prefix, uint32_t fallback_ms) {
    String h_key = String(prefix) + "_h";
    String m_key = String(prefix) + "_m";
    String s_key = String(prefix) + "_s";
    if (!server.hasArg(h_key) && !server.hasArg(m_key) && !server.hasArg(s_key)) return fallback_ms;
    uint32_t hours = server.hasArg(h_key) ? static_cast<uint32_t>(server.arg(h_key).toInt()) : 0;
    uint32_t minutes = server.hasArg(m_key) ? static_cast<uint32_t>(server.arg(m_key).toInt()) : 0;
    uint32_t seconds = server.hasArg(s_key) ? static_cast<uint32_t>(server.arg(s_key).toInt()) : 0;
    uint32_t total_seconds = hours * 3600UL + minutes * 60UL + seconds;
    return total_seconds * 1000UL;
}

static String status_to_json() {
    AppConfig cfg = config_get();
    AppRuntimeState state = app_state_get();
    EnvProfile active_profile;
    config_env_profile_get_active(active_profile);
    bool wifi_connected = network_is_wifi_connected();
    bool mqtt_connected = network_is_mqtt_connected();

    String json = "{";
    json += "\"device_id\":\"" + json_escape(cfg.device_id) + "\",";
    json += "\"ip\":\"" + (wifi_connected ? WiFi.localIP().toString() : String("")) + "\",";
    json += "\"wifi_connected\":" + String(wifi_connected ? "true" : "false") + ",";
    json += "\"mqtt_connected\":" + String(mqtt_connected ? "true" : "false") + ",";
    json += "\"rssi\":";
    json += wifi_connected ? String(network_get_rssi()) : String("null");
    json += ",";
    json += "\"temperature\":" + json_float_or_null(state.sensor_valid, state.temperature, 1) + ",";
    json += "\"humidity\":" + json_float_or_null(state.sensor_valid, state.humidity, 1) + ",";
    json += "\"battery_percent\":" + String(state.battery_percent) + ",";
    json += "\"battery_voltage\":" + String(state.battery_voltage, 3) + ",";
    json += "\"uptime_ms\":" + String(millis()) + ",";
    json += "\"last_mqtt_upload_ms\":";
    json += state.last_mqtt_upload_ms > 0 ? String(state.last_mqtt_upload_ms) : String("null");
    json += ",";
    uint32_t data_age_ms = app_state_data_age_ms(millis());
    json += "\"sensor_sample_interval_ms\":" + String(cfg.sensor_sample_interval_ms) + ",";
    json += "\"effective_sample_interval_ms\":" + String(config_get_effective_sample_interval_ms()) + ",";
    json += "\"report_interval_ms\":" + String(cfg.report_interval_ms) + ",";
    json += "\"display_refresh_interval_ms\":" + String(cfg.display_refresh_interval_ms) + ",";
    json += "\"report_enabled\":" + String(cfg.report_enabled ? "true" : "false") + ",";
    json += "\"display_enabled\":" + String(cfg.display_enabled ? "true" : "false") + ",";
    json += "\"report_pending\":" + String(state.report_pending ? "true" : "false") + ",";
    json += "\"display_pending\":" + String(state.display_pending ? "true" : "false") + ",";
    json += "\"data_age_ms\":";
    json += data_age_ms == UINT32_MAX ? String("null") : String(data_age_ms);
    json += ",";
    json += "\"data_fresh\":" + String(app_state_data_fresh(millis(), config_get_effective_sample_interval_ms()) ? "true" : "false") + ",";
    json += "\"env_level\":\"" + environment_level_to_string(state.env_level) + "\",";
    json += "\"env_reason\":\"" + environment_reason_to_string(state.env_reason) + "\",";
    json += "\"env_message_cn\":\"" + json_escape(state.env_message_cn) + "\",";
    json += "\"env_message_en\":\"" + json_escape(state.env_message_en) + "\",";
    json += "\"alarm_active\":" + String(state.alarm_active ? "true" : "false") + ",";
    json += "\"warning_active\":" + String(state.warning_active ? "true" : "false") + ",";
    json += alarm_manager_status_json() + ",";
    json += "\"history_count\":" + String(history_manager_count()) + ",";
    json += "\"history_capacity\":" + String(history_manager_capacity()) + ",";
    json += "\"history_last_seq\":" + String(history_manager_last_seq()) + ",";
    json += "\"environment_preset\":\"" + json_escape(cfg.environment_preset) + "\",";
    json += "\"active_env_profile_id\":\"" + json_escape(active_profile.id) + "\",";
    json += "\"active_env_profile_name\":\"" + json_escape(active_profile.name) + "\",";
    json += "\"env_profile_count\":" + String(cfg.env_profile_count) + ",";
    json += "\"temp_normal_min\":" + String(cfg.temp_normal_min, 1) + ",";
    json += "\"temp_normal_max\":" + String(cfg.temp_normal_max, 1) + ",";
    json += "\"temp_alarm_min\":" + String(cfg.temp_alarm_min, 1) + ",";
    json += "\"temp_alarm_max\":" + String(cfg.temp_alarm_max, 1) + ",";
    json += "\"humi_normal_min\":" + String(cfg.humi_normal_min, 1) + ",";
    json += "\"humi_normal_max\":" + String(cfg.humi_normal_max, 1) + ",";
    json += "\"humi_alarm_min\":" + String(cfg.humi_alarm_min, 1) + ",";
    json += "\"humi_alarm_max\":" + String(cfg.humi_alarm_max, 1) + ",";
    json += "\"firmware\":\"dev\"";
    json += "}";
    return json;
}

static String config_to_json(const AppConfig& cfg) {
    EnvProfile active_profile;
    config_env_profile_get_active(active_profile);
    String json = "{";
    json += "\"device_name\":\"" + json_escape(cfg.device_name) + "\",";
    json += "\"mqtt_host\":\"" + json_escape(cfg.mqtt_host) + "\",";
    json += "\"mqtt_port\":" + String(cfg.mqtt_port) + ",";
    json += "\"bemfa_uid\":\"" + json_escape(cfg.bemfa_uid) + "\",";
    json += "\"mqtt_topic_temp\":\"" + json_escape(cfg.mqtt_topic_temp) + "\",";
    json += "\"mqtt_topic_humi\":\"" + json_escape(cfg.mqtt_topic_humi) + "\",";
    json += "\"mqtt_topic_battery\":\"" + json_escape(cfg.mqtt_topic_battery) + "\",";
    json += "\"sample_interval_ms\":" + String(cfg.sample_interval_ms) + ",";
    json += "\"upload_interval_ms\":" + String(cfg.upload_interval_ms) + ",";
    json += "\"sensor_sample_interval_ms\":" + String(cfg.sensor_sample_interval_ms) + ",";
    json += "\"report_interval_ms\":" + String(cfg.report_interval_ms) + ",";
    json += "\"display_refresh_interval_ms\":" + String(cfg.display_refresh_interval_ms) + ",";
    json += "\"environment_preset\":\"" + json_escape(cfg.environment_preset) + "\",";
    json += "\"active_env_profile_id\":\"" + json_escape(active_profile.id) + "\",";
    json += "\"active_env_profile_name\":\"" + json_escape(active_profile.name) + "\",";
    json += "\"env_profile_count\":" + String(cfg.env_profile_count) + ",";
    json += "\"temp_normal_min\":" + String(cfg.temp_normal_min, 1) + ",";
    json += "\"temp_normal_max\":" + String(cfg.temp_normal_max, 1) + ",";
    json += "\"temp_alarm_min\":" + String(cfg.temp_alarm_min, 1) + ",";
    json += "\"temp_alarm_max\":" + String(cfg.temp_alarm_max, 1) + ",";
    json += "\"humi_normal_min\":" + String(cfg.humi_normal_min, 1) + ",";
    json += "\"humi_normal_max\":" + String(cfg.humi_normal_max, 1) + ",";
    json += "\"humi_alarm_min\":" + String(cfg.humi_alarm_min, 1) + ",";
    json += "\"humi_alarm_max\":" + String(cfg.humi_alarm_max, 1) + ",";
    json += "\"battery_voltage_factor\":" + String(cfg.battery_voltage_factor, 3);
    json += "}";
    return json;
}

static String extract_json_string(const String& body, const char* key, const String& fallback) {
    String needle = String("\"") + key + "\"";
    int key_pos = body.indexOf(needle);
    if (key_pos < 0) return fallback;
    int colon = body.indexOf(':', key_pos + needle.length());
    if (colon < 0) return fallback;
    int first_quote = body.indexOf('"', colon + 1);
    if (first_quote < 0) return fallback;
    int second_quote = body.indexOf('"', first_quote + 1);
    if (second_quote < 0) return fallback;
    return body.substring(first_quote + 1, second_quote);
}

static String extract_json_token(const String& body, const char* key, const String& fallback) {
    String needle = String("\"") + key + "\"";
    int key_pos = body.indexOf(needle);
    if (key_pos < 0) return fallback;
    int colon = body.indexOf(':', key_pos + needle.length());
    if (colon < 0) return fallback;
    int end = colon + 1;
    while (end < body.length() && isspace(body[end])) end++;
    int start = end;
    while (end < body.length() && body[end] != ',' && body[end] != '}') end++;
    String token = body.substring(start, end);
    token.trim();
    return token.length() ? token : fallback;
}

static String get_arg_or_json(const String& body, const char* key, const String& fallback) {
    if (server.hasArg(key)) return server.arg(key);
    if (body.length()) return extract_json_string(body, key, fallback);
    return fallback;
}

static uint32_t get_u32_arg_or_json(const String& body, const char* key, uint32_t fallback) {
    if (server.hasArg(key)) return static_cast<uint32_t>(server.arg(key).toInt());
    if (body.length()) return static_cast<uint32_t>(extract_json_token(body, key, String(fallback)).toInt());
    return fallback;
}

static float get_float_arg_or_json(const String& body, const char* key, float fallback) {
    if (server.hasArg(key)) return server.arg(key).toFloat();
    if (body.length()) return extract_json_token(body, key, String(fallback, 3)).toFloat();
    return fallback;
}

static bool get_bool_arg_or_json(const String& body, const char* key, bool fallback) {
    String value;
    if (server.hasArg(key)) {
        value = server.arg(key);
    } else if (body.length()) {
        value = extract_json_token(body, key, fallback ? "true" : "false");
    } else {
        return fallback;
    }
    value.trim();
    value.toLowerCase();
    return value == "1" || value == "true" || value == "on" || value == "yes";
}


static void handle_root() {
    AppConfig cfg = config_get();
    String body;
    body += "<header><h1 data-zh=\"ESP32-S3 &#x58A8;&#x6C34;&#x5C4F;&#x6E29;&#x6E7F;&#x5EA6;&#x7EC8;&#x7AEF;\" data-en=\"ESP32-S3 E-Ink Temperature & Humidity Terminal\">ESP32-S3 &#x58A8;&#x6C34;&#x5C4F;&#x6E29;&#x6E7F;&#x5EA6;&#x7EC8;&#x7AEF;</h1>";
    body += "<button type=\"button\" id=\"langBtn\">English</button></header>";
    body += "<section><h2 data-zh=\"&#x5F53;&#x524D;&#x72B6;&#x6001;\" data-en=\"Current Status\">&#x5F53;&#x524D;&#x72B6;&#x6001;</h2>";
    body += "<p id=\"status_notice\" class=\"muted\"></p><div class=\"status-grid\">";
    body += "<div class=\"status-row\"><span data-zh=\"&#x6E29;&#x5EA6;\" data-en=\"Temperature\">&#x6E29;&#x5EA6;</span><span class=\"value\" id=\"st_temp\">--</span></div>";
    body += "<div class=\"status-row\"><span data-zh=\"&#x6E7F;&#x5EA6;\" data-en=\"Humidity\">&#x6E7F;&#x5EA6;</span><span class=\"value\" id=\"st_humi\">--</span></div>";
    body += "<div class=\"status-row\" id=\"env_card\"><span data-zh=\"&#x73AF;&#x5883;&#x72B6;&#x6001;\" data-en=\"Environment\">&#x73AF;&#x5883;&#x72B6;&#x6001;</span><span class=\"value\" id=\"st_env\">--</span><div class=\"muted\" id=\"st_env_reason\">--</div></div>";
    body += "<div class=\"status-row\"><span data-zh=\"&#x7535;&#x6C60;\" data-en=\"Battery\">&#x7535;&#x6C60;</span><span class=\"value\" id=\"st_battery\">--</span></div>";
    body += "<div class=\"status-row\"><span data-zh=\"&#x7F51;&#x7EDC;&#x72B6;&#x6001;\" data-en=\"Network Status\">&#x7F51;&#x7EDC;&#x72B6;&#x6001;</span><span class=\"value\" id=\"st_wifi\">--</span></div>";
    body += "<div class=\"status-row\"><span data-zh=\"&#x8FDC;&#x7A0B;&#x8FDE;&#x63A5;&#x72B6;&#x6001;\" data-en=\"Remote Connection\">&#x8FDC;&#x7A0B;&#x8FDE;&#x63A5;&#x72B6;&#x6001;</span><span class=\"value\" id=\"st_mqtt\">--</span></div>";
    body += "<div class=\"status-row\"><span data-zh=\"&#x8BBE;&#x5907; IP\" data-en=\"Device IP\">&#x8BBE;&#x5907; IP</span><span class=\"value\" id=\"st_ip\">--</span></div>";
    body += "</div></section>";
    body += "<section><h2 data-zh=\"&#x7B80;&#x5355;&#x8BBE;&#x5907;&#x8BBE;&#x7F6E;\" data-en=\"Basic Settings\">&#x7B80;&#x5355;&#x8BBE;&#x5907;&#x8BBE;&#x7F6E;</h2>";
    body += "<form method=\"POST\" action=\"/save-mqtt\">";
    body += "<input type=\"hidden\" name=\"device_id\" value=\"" + html_escape(cfg.device_id) + "\">";
    body += "<label data-zh=\"&#x8BBE;&#x5907;&#x540D;&#x79F0;\" data-en=\"Device Name\">&#x8BBE;&#x5907;&#x540D;&#x79F0;</label><input name=\"device_name\" value=\"" + html_escape(cfg.device_name) + "\">";
    body += "<label data-zh=\"&#x91C7;&#x6837;&#x95F4;&#x9694;\" data-en=\"Sensor Sample Interval\">&#x91C7;&#x6837;&#x95F4;&#x9694;</label><div class=\"hms\"><input id=\"sensor_h\" name=\"sensor_h\" type=\"number\" min=\"0\" value=\"" + String(interval_hours(cfg.sensor_sample_interval_ms)) + "\"><span data-zh=\"&#x5C0F;&#x65F6;\" data-en=\"h\">&#x5C0F;&#x65F6;</span><input id=\"sensor_m\" name=\"sensor_m\" type=\"number\" min=\"0\" value=\"" + String(interval_minutes(cfg.sensor_sample_interval_ms)) + "\"><span data-zh=\"&#x5206;&#x949F;\" data-en=\"m\">&#x5206;&#x949F;</span><input id=\"sensor_s\" name=\"sensor_s\" type=\"number\" min=\"0\" value=\"" + String(interval_seconds(cfg.sensor_sample_interval_ms)) + "\"><span data-zh=\"&#x79D2;\" data-en=\"s\">&#x79D2;</span></div>";
    body += "<label data-zh=\"&#x4E0A;&#x62A5;&#x95F4;&#x9694;\" data-en=\"Report Interval\">&#x4E0A;&#x62A5;&#x95F4;&#x9694;</label><div class=\"hms\"><input id=\"report_h\" name=\"report_h\" type=\"number\" min=\"0\" value=\"" + String(interval_hours(cfg.report_interval_ms)) + "\"><span data-zh=\"&#x5C0F;&#x65F6;\" data-en=\"h\">&#x5C0F;&#x65F6;</span><input id=\"report_m\" name=\"report_m\" type=\"number\" min=\"0\" value=\"" + String(interval_minutes(cfg.report_interval_ms)) + "\"><span data-zh=\"&#x5206;&#x949F;\" data-en=\"m\">&#x5206;&#x949F;</span><input id=\"report_s\" name=\"report_s\" type=\"number\" min=\"0\" value=\"" + String(interval_seconds(cfg.report_interval_ms)) + "\"><span data-zh=\"&#x79D2;\" data-en=\"s\">&#x79D2;</span></div>";
    body += "<label data-zh=\"&#x5C4F;&#x5E55;&#x5237;&#x65B0;&#x95F4;&#x9694;\" data-en=\"Display Refresh Interval\">&#x5C4F;&#x5E55;&#x5237;&#x65B0;&#x95F4;&#x9694;</label><div class=\"hms\"><input id=\"display_h\" name=\"display_h\" type=\"number\" min=\"0\" value=\"" + String(interval_hours(cfg.display_refresh_interval_ms)) + "\"><span data-zh=\"&#x5C0F;&#x65F6;\" data-en=\"h\">&#x5C0F;&#x65F6;</span><input id=\"display_m\" name=\"display_m\" type=\"number\" min=\"0\" value=\"" + String(interval_minutes(cfg.display_refresh_interval_ms)) + "\"><span data-zh=\"&#x5206;&#x949F;\" data-en=\"m\">&#x5206;&#x949F;</span><input id=\"display_s\" name=\"display_s\" type=\"number\" min=\"0\" value=\"" + String(interval_seconds(cfg.display_refresh_interval_ms)) + "\"><span data-zh=\"&#x79D2;\" data-en=\"s\">&#x79D2;</span></div>";
    body += "<p class=\"muted\" data-zh=\"&#x91C7;&#x6837;&#x95F4;&#x9694;&#x4F1A;&#x81EA;&#x52A8;&#x9002;&#x914D;&#x6700;&#x5FEB;&#x7684;&#x4E0A;&#x62A5;&#x6216;&#x5C4F;&#x5E55;&#x5237;&#x65B0;&#x95F4;&#x9694;&#xFF1B;&#x8FC7;&#x5C0F;&#x7684;&#x95F4;&#x9694;&#x4F1A;&#x81EA;&#x52A8;&#x8C03;&#x6574;&#x5230;&#x786C;&#x4EF6;&#x5B89;&#x5168;&#x503C;&#x3002;\" data-en=\"The sampling interval is automatically adjusted to match the fastest report or display refresh interval. Too-small values are clamped to safe hardware limits.\">&#x91C7;&#x6837;&#x95F4;&#x9694;&#x4F1A;&#x81EA;&#x52A8;&#x9002;&#x914D;&#x6700;&#x5FEB;&#x7684;&#x4E0A;&#x62A5;&#x6216;&#x5C4F;&#x5E55;&#x5237;&#x65B0;&#x95F4;&#x9694;&#xFF1B;&#x8FC7;&#x5C0F;&#x7684;&#x95F4;&#x9694;&#x4F1A;&#x81EA;&#x52A8;&#x8C03;&#x6574;&#x5230;&#x786C;&#x4EF6;&#x5B89;&#x5168;&#x503C;&#x3002;</p>";
    body += "<p id=\"interval_preview\" class=\"muted\"></p>";
    body += "<details><summary data-zh=\"&#x9AD8;&#x7EA7;&#x8BBE;&#x7F6E;\" data-en=\"Advanced Settings\">&#x9AD8;&#x7EA7;&#x8BBE;&#x7F6E;</summary>";
    body += "<p class=\"muted\" data-zh=\"&#x666E;&#x901A;&#x7528;&#x6237;&#x65E0;&#x9700;&#x4FEE;&#x6539;&#x6B64;&#x5904;&#xFF1B;&#x4EC5;&#x5728;&#x4F7F;&#x7528;&#x81EA;&#x5B9A;&#x4E49;&#x8FDC;&#x7A0B;&#x670D;&#x52A1;&#x65F6;&#x914D;&#x7F6E;&#x3002;\" data-en=\"Most users do not need to change this. Configure it only when using a custom remote service.\">&#x666E;&#x901A;&#x7528;&#x6237;&#x65E0;&#x9700;&#x4FEE;&#x6539;&#x6B64;&#x5904;&#xFF1B;&#x4EC5;&#x5728;&#x4F7F;&#x7528;&#x81EA;&#x5B9A;&#x4E49;&#x8FDC;&#x7A0B;&#x670D;&#x52A1;&#x65F6;&#x914D;&#x7F6E;&#x3002;</p>";
    body += "<label>MQTT Host</label><input name=\"mqtt_host\" value=\"" + html_escape(cfg.mqtt_host) + "\">";
    body += "<label>MQTT Port</label><input name=\"mqtt_port\" type=\"number\" value=\"" + String(cfg.mqtt_port) + "\">";
    body += "<label>MQTT Client ID</label><input name=\"mqtt_client_id\" value=\"" + html_escape(cfg.mqtt_client_id.length() ? cfg.mqtt_client_id : cfg.device_id) + "\">";
    body += "<label>Username</label><input name=\"mqtt_username\" value=\"" + html_escape(cfg.mqtt_username) + "\">";
    body += "<label>Password</label><input name=\"mqtt_password\" type=\"password\"><div class=\"muted\" data-zh=\"&#x7559;&#x7A7A;&#x5219;&#x4FDD;&#x7559;&#x539F;&#x5BC6;&#x7801;\" data-en=\"Leave blank to keep current password\">&#x7559;&#x7A7A;&#x5219;&#x4FDD;&#x7559;&#x539F;&#x5BC6;&#x7801;</div>";
    body += "<label>Topic Prefix</label><input name=\"topic_prefix\" value=\"" + html_escape(cfg.topic_prefix) + "\">";
    body += "</details>";
    body += "<button class=\"primary\" type=\"button\" id=\"saveBtn\" data-zh=\"&#x4FDD;&#x5B58;&#x7CFB;&#x7EDF;&#x914D;&#x7F6E;\" data-en=\"Save Settings\">&#x4FDD;&#x5B58;&#x7CFB;&#x7EDF;&#x914D;&#x7F6E;</button></form></section>";
    body += "<section class=\"env-profile-panel\"><h2 data-zh=\"&#x73AF;&#x5883;&#x9884;&#x8BBE;\" data-en=\"Environment Profiles\">&#x73AF;&#x5883;&#x9884;&#x8BBE;</h2><p class=\"muted\" data-zh=\"&#x7528;&#x4E8E;&#x5B9A;&#x4E49;&#x4E0D;&#x540C;&#x573A;&#x666F;&#x4E0B;&#x7684;&#x6E29;&#x6E7F;&#x5EA6;&#x9002;&#x5B9C;&#x8303;&#x56F4;&#x548C;&#x62A5;&#x8B66;&#x8303;&#x56F4;&#x3002;\" data-en=\"Define comfortable and alarm ranges for different environments.\">&#x7528;&#x4E8E;&#x5B9A;&#x4E49;&#x4E0D;&#x540C;&#x573A;&#x666F;&#x4E0B;&#x7684;&#x6E29;&#x6E7F;&#x5EA6;&#x9002;&#x5B9C;&#x8303;&#x56F4;&#x548C;&#x62A5;&#x8B66;&#x8303;&#x56F4;&#x3002;</p>";
    body += "<div class=\"profile-topbar\"><div><label data-zh=\"&#x5F53;&#x524D;&#x4F7F;&#x7528;\" data-en=\"Active Profile\">&#x5F53;&#x524D;&#x4F7F;&#x7528;</label><select id=\"env_profile_select\"></select></div><button type=\"button\" id=\"envApplyBtn\" data-zh=\"&#x5E94;&#x7528;&#x6B64;&#x9884;&#x8BBE;\" data-en=\"Apply This Profile\">&#x5E94;&#x7528;&#x6B64;&#x9884;&#x8BBE;</button><button type=\"button\" id=\"envNewBtn\" data-zh=\"&#x65B0;&#x589E;&#x9884;&#x8BBE;\" data-en=\"New Profile\">&#x65B0;&#x589E;&#x9884;&#x8BBE;</button></div>";
    body += "<div id=\"env_edit_card\" class=\"profile-edit-card\"><div class=\"edit-header\"><h3 id=\"env_edit_title\" data-zh=\"&#x5F53;&#x524D;&#x9884;&#x8BBE;&#x8BBE;&#x7F6E;\" data-en=\"Current Profile Settings\">&#x5F53;&#x524D;&#x9884;&#x8BBE;&#x8BBE;&#x7F6E;</h3><span id=\"env_edit_mode\" class=\"badge\" data-zh=\"&#x7F16;&#x8F91;&#x73B0;&#x6709;&#x9884;&#x8BBE;\" data-en=\"Editing existing profile\">&#x7F16;&#x8F91;&#x73B0;&#x6709;&#x9884;&#x8BBE;</span></div>";
    body += "<input type=\"hidden\" id=\"env_profile_id\" value=\"" + html_escape(cfg.active_env_profile_id) + "\"><label data-zh=\"&#x540D;&#x79F0;\" data-en=\"Name\">&#x540D;&#x79F0;</label><input id=\"env_profile_name\" value=\"" + html_escape(cfg.active_env_profile_name) + "\">";
    body += "<div class=\"threshold-grid\"><div class=\"threshold-card\"><h4 data-zh=\"&#x6E29;&#x5EA6;\" data-en=\"Temperature\">&#x6E29;&#x5EA6;</h4><div class=\"threshold-table\"><div class=\"threshold-head\"><span data-zh=\"&#x7C7B;&#x578B;\" data-en=\"Type\">&#x7C7B;&#x578B;</span><span data-zh=\"&#x4E0B;&#x9650;\" data-en=\"Low\">&#x4E0B;&#x9650;</span><span data-zh=\"&#x4E0A;&#x9650;\" data-en=\"High\">&#x4E0A;&#x9650;</span><span data-zh=\"&#x5355;&#x4F4D;\" data-en=\"Unit\">&#x5355;&#x4F4D;</span></div><div class=\"threshold-line comfort-row\"><span class=\"threshold-label\" data-zh=\"&#x9002;&#x5B9C;&#x8303;&#x56F4;\" data-en=\"Normal range\">&#x9002;&#x5B9C;&#x8303;&#x56F4;</span><div class=\"threshold-cell value-only\"><input id=\"temp_normal_min\" type=\"number\" step=\"0.1\" value=\"" + String(cfg.temp_normal_min, 1) + "\"></div><div class=\"threshold-cell with-op\"><span class=\"op\">~</span><input id=\"temp_normal_max\" type=\"number\" step=\"0.1\" value=\"" + String(cfg.temp_normal_max, 1) + "\"></div><span class=\"threshold-unit\">C</span></div><div class=\"threshold-line alarm-threshold-row\"><span class=\"threshold-label\" data-zh=\"&#x62A5;&#x8B66;&#x8303;&#x56F4;\" data-en=\"Alarm range\">&#x62A5;&#x8B66;&#x8303;&#x56F4;</span><div class=\"threshold-cell with-op\"><span class=\"op\">&lt;</span><input id=\"temp_alarm_min\" type=\"number\" step=\"0.1\" value=\"" + String(cfg.temp_alarm_min, 1) + "\"></div><div class=\"threshold-cell with-op\"><span class=\"op\">&gt;</span><input id=\"temp_alarm_max\" type=\"number\" step=\"0.1\" value=\"" + String(cfg.temp_alarm_max, 1) + "\"></div><span class=\"threshold-unit\">C</span></div></div></div>";
    body += "<div class=\"threshold-card\"><h4 data-zh=\"&#x6E7F;&#x5EA6;\" data-en=\"Humidity\">&#x6E7F;&#x5EA6;</h4><div class=\"threshold-table\"><div class=\"threshold-head\"><span data-zh=\"&#x7C7B;&#x578B;\" data-en=\"Type\">&#x7C7B;&#x578B;</span><span data-zh=\"&#x4E0B;&#x9650;\" data-en=\"Low\">&#x4E0B;&#x9650;</span><span data-zh=\"&#x4E0A;&#x9650;\" data-en=\"High\">&#x4E0A;&#x9650;</span><span data-zh=\"&#x5355;&#x4F4D;\" data-en=\"Unit\">&#x5355;&#x4F4D;</span></div><div class=\"threshold-line comfort-row\"><span class=\"threshold-label\" data-zh=\"&#x9002;&#x5B9C;&#x8303;&#x56F4;\" data-en=\"Normal range\">&#x9002;&#x5B9C;&#x8303;&#x56F4;</span><div class=\"threshold-cell value-only\"><input id=\"humi_normal_min\" type=\"number\" step=\"0.1\" value=\"" + String(cfg.humi_normal_min, 1) + "\"></div><div class=\"threshold-cell with-op\"><span class=\"op\">~</span><input id=\"humi_normal_max\" type=\"number\" step=\"0.1\" value=\"" + String(cfg.humi_normal_max, 1) + "\"></div><span class=\"threshold-unit\">%</span></div><div class=\"threshold-line alarm-threshold-row\"><span class=\"threshold-label\" data-zh=\"&#x62A5;&#x8B66;&#x8303;&#x56F4;\" data-en=\"Alarm range\">&#x62A5;&#x8B66;&#x8303;&#x56F4;</span><div class=\"threshold-cell with-op\"><span class=\"op\">&lt;</span><input id=\"humi_alarm_min\" type=\"number\" step=\"0.1\" value=\"" + String(cfg.humi_alarm_min, 1) + "\"></div><div class=\"threshold-cell with-op\"><span class=\"op\">&gt;</span><input id=\"humi_alarm_max\" type=\"number\" step=\"0.1\" value=\"" + String(cfg.humi_alarm_max, 1) + "\"></div><span class=\"threshold-unit\">%</span></div></div></div></div>";
    body += "<div class=\"profile-actions\"><button type=\"button\" id=\"envSaveBtn\" data-zh=\"&#x4FDD;&#x5B58;&#x5F53;&#x524D;&#x9884;&#x8BBE;\" data-en=\"Save Current Profile\">&#x4FDD;&#x5B58;&#x5F53;&#x524D;&#x9884;&#x8BBE;</button><button type=\"button\" id=\"envSaveNewBtn\" data-zh=\"&#x4FDD;&#x5B58;&#x65B0;&#x9884;&#x8BBE;\" data-en=\"Save New Profile\">&#x4FDD;&#x5B58;&#x65B0;&#x9884;&#x8BBE;</button><button type=\"button\" id=\"envDeleteBtn\" class=\"danger\" data-zh=\"&#x5220;&#x9664;&#x5F53;&#x524D;&#x9884;&#x8BBE;\" data-en=\"Delete Current Profile\">&#x5220;&#x9664;&#x5F53;&#x524D;&#x9884;&#x8BBE;</button><button type=\"button\" id=\"envCancelNewBtn\" data-zh=\"&#x53D6;&#x6D88;&#x65B0;&#x589E;\" data-en=\"Cancel New\">&#x53D6;&#x6D88;&#x65B0;&#x589E;</button></div><p id=\"env_profile_notice\" class=\"muted\"></p></div></section>";
    body += "<section><h2 data-zh=\"&#x62A5;&#x8B66;&#x8BB0;&#x5F55;\" data-en=\"Alarm Records\">&#x62A5;&#x8B66;&#x8BB0;&#x5F55;</h2><p id=\"alarm_summary\" class=\"muted\">--</p><div class=\"alarm-actions\"><button type=\"button\" id=\"ackAllAlarmsBtn\" data-zh=\"&#x786E;&#x8BA4;&#x5168;&#x90E8;&#x62A5;&#x8B66;\" data-en=\"Acknowledge All\">&#x786E;&#x8BA4;&#x5168;&#x90E8;&#x62A5;&#x8B66;</button><span id=\"alarm_notice\" class=\"muted\"></span></div><div style=\"overflow-x:auto\"><table class=\"alarm-table\"><thead><tr><th data-zh=\"&#x72B6;&#x6001;\" data-en=\"Status\">&#x72B6;&#x6001;</th><th data-zh=\"&#x539F;&#x56E0;\" data-en=\"Reason\">&#x539F;&#x56E0;</th><th data-zh=\"&#x5F00;&#x59CB;\" data-en=\"Start\">&#x5F00;&#x59CB;</th><th data-zh=\"&#x6301;&#x7EED;\" data-en=\"Duration\">&#x6301;&#x7EED;</th><th data-zh=\"&#x6700;&#x9AD8;&#x6E29;&#x5EA6;\" data-en=\"Max Temp\">&#x6700;&#x9AD8;&#x6E29;&#x5EA6;</th><th data-zh=\"&#x6700;&#x9AD8;&#x6E7F;&#x5EA6;\" data-en=\"Max Humi\">&#x6700;&#x9AD8;&#x6E7F;&#x5EA6;</th><th data-zh=\"&#x786E;&#x8BA4;\" data-en=\"Ack\">&#x786E;&#x8BA4;</th><th data-zh=\"&#x64CD;&#x4F5C;\" data-en=\"Action\">&#x64CD;&#x4F5C;</th></tr></thead><tbody id=\"alarm_rows\"><tr><td colspan=\"8\">--</td></tr></tbody></table></div></section>";
    body += "<section><h2 data-zh=\"&#x5386;&#x53F2;&#x8D8B;&#x52BF;\" data-en=\"History Trend\">&#x5386;&#x53F2;&#x8D8B;&#x52BF;</h2><div class=\"history-chart-card\"><div class=\"chart-toolbar\"><button type=\"button\" id=\"refreshChartBtn\" data-zh=\"&#x5237;&#x65B0;&#x8D8B;&#x52BF;\" data-en=\"Refresh Trend\">&#x5237;&#x65B0;&#x8D8B;&#x52BF;</button><label><input type=\"checkbox\" id=\"chart_show_temp\" checked> <span data-zh=\"&#x663E;&#x793A;&#x6E29;&#x5EA6;\" data-en=\"Show temperature\">&#x663E;&#x793A;&#x6E29;&#x5EA6;</span></label><label><input type=\"checkbox\" id=\"chart_show_humi\" checked> <span data-zh=\"&#x663E;&#x793A;&#x6E7F;&#x5EA6;\" data-en=\"Show humidity\">&#x663E;&#x793A;&#x6E7F;&#x5EA6;</span></label></div><p id=\"history_chart_notice\" class=\"muted\" data-zh=\"&#x70B9;&#x51FB;&#x5237;&#x65B0;&#x8D8B;&#x52BF;&#x52A0;&#x8F7D;&#x5386;&#x53F2;&#x6570;&#x636E;\" data-en=\"Click Refresh Trend to load history data\">&#x70B9;&#x51FB;&#x5237;&#x65B0;&#x8D8B;&#x52BF;&#x52A0;&#x8F7D;&#x5386;&#x53F2;&#x6570;&#x636E;</p><div class=\"chart-wrap\"><canvas id=\"history_chart\"></canvas><div id=\"history_chart_tooltip\" class=\"chart-tooltip\"></div></div><div class=\"chart-legend\"><span class=\"legend-temp\" data-zh=\"&#x6E29;&#x5EA6; C\" data-en=\"Temperature C\">&#x6E29;&#x5EA6; C</span><span class=\"legend-humi\" data-zh=\"&#x6E7F;&#x5EA6; %\" data-en=\"Humidity %\">&#x6E7F;&#x5EA6; %</span><span class=\"muted\">ALARM</span></div></div></section>";
    body += "<section><h2 data-zh=\"&#x5386;&#x53F2;&#x6570;&#x636E;\" data-en=\"History Data\">&#x5386;&#x53F2;&#x6570;&#x636E;</h2><p id=\"history_summary\" class=\"muted\">--</p><div class=\"history-actions\"><button type=\"button\" id=\"refreshHistoryBtn\" data-zh=\"&#x5237;&#x65B0;&#x5386;&#x53F2;&#x6570;&#x636E;\" data-en=\"Refresh History\">&#x5237;&#x65B0;&#x5386;&#x53F2;&#x6570;&#x636E;</button><span id=\"history_notice\" class=\"muted\"></span></div><div style=\"overflow-x:auto\"><table class=\"history-table\"><thead><tr><th data-zh=\"&#x65F6;&#x95F4;\" data-en=\"Time\">&#x65F6;&#x95F4;</th><th data-zh=\"&#x6E29;&#x5EA6;\" data-en=\"Temp\">&#x6E29;&#x5EA6;</th><th data-zh=\"&#x6E7F;&#x5EA6;\" data-en=\"Humi\">&#x6E7F;&#x5EA6;</th><th data-zh=\"&#x73AF;&#x5883;&#x72B6;&#x6001;\" data-en=\"Env\">&#x73AF;&#x5883;&#x72B6;&#x6001;</th><th data-zh=\"&#x62A5;&#x8B66;&#x72B6;&#x6001;\" data-en=\"Alarm\">&#x62A5;&#x8B66;&#x72B6;&#x6001;</th></tr></thead><tbody id=\"history_rows\"><tr><td colspan=\"5\">--</td></tr></tbody></table></div></section>";
    body += "<section><h2 data-zh=\"&#x8BBE;&#x5907;&#x7EF4;&#x62A4;\" data-en=\"Device Maintenance\">&#x8BBE;&#x5907;&#x7EF4;&#x62A4;</h2><div class=\"actions\"><button type=\"button\" id=\"restartBtn\" data-zh=\"&#x91CD;&#x542F;&#x8BBE;&#x5907;\" data-en=\"Restart Device\">&#x91CD;&#x542F;&#x8BBE;&#x5907;</button><a class=\"button\" href=\"/\" data-zh=\"&#x8FD4;&#x56DE;&#x9996;&#x9875;\" data-en=\"Back to Home\">&#x8FD4;&#x56DE;&#x9996;&#x9875;</a><button type=\"button\" class=\"danger\" disabled data-zh=\"&#x6062;&#x590D;&#x9ED8;&#x8BA4;&#x914D;&#x7F6E;&#xFF08;TODO&#xFF09;\" data-en=\"Restore Defaults (TODO)\">&#x6062;&#x590D;&#x9ED8;&#x8BA4;&#x914D;&#x7F6E;&#xFF08;TODO&#xFF09;</button></div><p id=\"maintenance_notice\" class=\"muted\"></p></section>";
    body += language_toggle_script();
    body += "<script>";
    body += "function setText(id,text){var el=document.getElementById(id);if(el)el.textContent=text;}function dash(v){return v===null||v===undefined||v===''?'--':v;}function lang(){return localStorage.getItem('lang')||'zh';}function yesNo(v){return lang()==='zh'?(v?'\u5df2\u8fde\u63a5':'\u672a\u8fde\u63a5'):(v?'connected':'disconnected');}";
    body += "var DHT11_SAFE_MIN_SAMPLE_MS=2000,REPORT_SAFE_MIN_INTERVAL_MS=2000,DISPLAY_SAFE_MIN_REFRESH_MS=5000;var reportEnabled=true,displayEnabled=true,allowSubmit=false;";
    body += "function splitMsToHms(ms){var t=Math.floor(ms/1000),h=Math.floor(t/3600);t%=3600;var m=Math.floor(t/60),s=t%60;return{h:h,m:m,s:s};}function hmsToMs(p){var h=Number(document.getElementById(p+'_h').value||0),m=Number(document.getElementById(p+'_m').value||0),s=Number(document.getElementById(p+'_s').value||0);return ((h*3600)+(m*60)+s)*1000;}function writeHms(p,ms){var v=splitMsToHms(ms);document.getElementById(p+'_h').value=v.h;document.getElementById(p+'_m').value=v.m;document.getElementById(p+'_s').value=v.s;}function hmsText(ms){var v=splitMsToHms(ms);return lang()==='zh'?(v.h+'\u5c0f\u65f6 '+v.m+'\u5206\u949f '+v.s+'\u79d2'):(v.h+'h '+v.m+'m '+v.s+'s');}";
    body += "function normalizeAllIntervals(){var sensor=hmsToMs('sensor'),report=hmsToMs('report'),display=hmsToMs('display');if(sensor<DHT11_SAFE_MIN_SAMPLE_MS)sensor=DHT11_SAFE_MIN_SAMPLE_MS;if(report<REPORT_SAFE_MIN_INTERVAL_MS)report=REPORT_SAFE_MIN_INTERVAL_MS;if(display<DISPLAY_SAFE_MIN_REFRESH_MS)display=DISPLAY_SAFE_MIN_REFRESH_MS;if(reportEnabled&&report<sensor)sensor=report;if(displayEnabled&&display<sensor)sensor=display;if(sensor<DHT11_SAFE_MIN_SAMPLE_MS)sensor=DHT11_SAFE_MIN_SAMPLE_MS;writeHms('sensor',sensor);writeHms('report',report);writeHms('display',display);return{sensor:sensor,report:report,display:display};}function updateIntervalPreview(){var v=normalizeAllIntervals();setText('interval_preview',(lang()==='zh'?'\u5b9e\u9645\u5c06\u4fdd\u5b58\uff1a\u91c7\u6837 ':'Will save: sensor ')+hmsText(v.sensor)+(lang()==='zh'?'\uff0c\u4e0a\u62a5 ':' , report ')+hmsText(v.report)+(lang()==='zh'?'\uff0c\u5c4f\u5e55\u5237\u65b0 ':' , display ')+hmsText(v.display));}";
    body += "function renderStatus(d){reportEnabled=d.report_enabled!==false;displayEnabled=d.display_enabled!==false;setText('st_ip',dash(d.ip));setText('st_wifi',yesNo(d.wifi_connected));setText('st_mqtt',yesNo(d.mqtt_connected));setText('st_temp',d.temperature===null?'--':Number(d.temperature).toFixed(1)+' C');setText('st_humi',d.humidity===null?'--':Number(d.humidity).toFixed(1)+' %RH');setText('st_battery',(d.battery_percent===null?'--':d.battery_percent+' %')+(d.battery_voltage===null?'':' / '+Number(d.battery_voltage).toFixed(3)+' V'));setText('st_env',lang()==='zh'?(d.env_level==='alarm'?'\u62a5\u8b66':(d.env_level==='warning'?'\u6ce8\u610f':'\u9002\u5b9c')):(d.env_level||'normal'));setText('st_env_reason',lang()==='zh'?dash(d.env_message_cn):dash(d.env_message_en));var c=document.getElementById('env_card');if(c)c.className='status-row env-'+(d.env_level||'normal');setText('status_notice','');}async function refreshStatus(){try{var r=await fetch('/api/status',{cache:'no-store'});if(!r.ok)throw new Error('bad status');renderStatus(await r.json());}catch(e){setText('status_notice',lang()==='zh'?'\u8bbe\u5907\u6682\u65f6\u65e0\u54cd\u5e94':'Device temporarily unavailable');}}";
    body += "var restartBtn=document.getElementById('restartBtn');if(restartBtn){restartBtn.onclick=async function(){setText('maintenance_notice',lang()==='zh'?'\u8bbe\u5907\u6b63\u5728\u91cd\u542f\uff0c\u8bf7\u7a0d\u540e\u5237\u65b0\u9875\u9762\u3002':'Device is restarting. Please refresh later.');try{await fetch('/api/restart',{method:'POST'});}catch(e){}};}";
    body += "var envProfiles=[],activeEnvProfileId='',envEditMode='edit';function field(id){return document.getElementById(id);}function envMsg(zh,en){return lang()==='zh'?zh:en;}function setEnvNotice(t){setText('env_profile_notice',t);}function apiMessage(err,fallback){try{var o=JSON.parse(String(err.message||err));return o.message||fallback;}catch(e){return fallback;}}";
    body += "function setVisible(id,show){var el=field(id);if(el)el.style.display=show?'':'none';}function setEnvMode(mode){envEditMode=mode;var create=mode==='create';var title=field('env_edit_title'),badge=field('env_edit_mode'),name=field('env_profile_name');if(title){title.textContent=envMsg(create?'\\u65b0\\u589e\\u73af\\u5883\\u9884\\u8bbe':'\\u5f53\\u524d\\u9884\\u8bbe\\u8bbe\\u7f6e',create?'New Environment Profile':'Current Profile Settings');}if(badge){badge.textContent=envMsg(create?'\\u65b0\\u5efa\\u9884\\u8bbe':'\\u7f16\\u8f91\\u73b0\\u6709\\u9884\\u8bbe',create?'New profile':'Editing existing profile');}if(name)name.placeholder=create?envMsg('\\u8bf7\\u8f93\\u5165\\u9884\\u8bbe\\u540d\\u79f0','Please enter a profile name'):'';setVisible('envSaveBtn',!create);setVisible('envSaveNewBtn',create);setVisible('envDeleteBtn',!create);setVisible('envCancelNewBtn',create);}";
    body += "function fillEnvProfile(p){if(!p)return;field('env_profile_id').value=p.id;field('env_profile_name').value=p.name;['temp_normal_min','temp_normal_max','temp_alarm_min','temp_alarm_max','humi_normal_min','humi_normal_max','humi_alarm_min','humi_alarm_max'].forEach(function(k){field(k).value=p[k];});}function selectedEnvProfile(){var id=field('env_profile_select').value;return envProfiles.find(function(p){return p.id===id;});}";
    body += "async function loadEnvProfiles(preferId){try{var r=await fetch('/api/env/profiles',{cache:'no-store'});var d=await r.json();envProfiles=d.profiles||[];activeEnvProfileId=d.active_env_profile_id||'';var sel=field('env_profile_select');if(sel){sel.innerHTML='';envProfiles.forEach(function(p){var o=document.createElement('option');o.value=p.id;o.textContent=p.name;if(p.id===(preferId||activeEnvProfileId))o.selected=true;sel.appendChild(o);});fillEnvProfile(selectedEnvProfile()||envProfiles[0]);setEnvMode('edit');}}catch(e){setEnvNotice(envMsg('\\u52a0\\u8f7d\\u9884\\u8bbe\\u5931\\u8d25','Profile load failed'));}}async function postForm(url,data){var body=new URLSearchParams(data);var r=await fetch(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:body});var text=await r.text();if(!r.ok)throw new Error(text);return text;}function currentEnvProfilePayload(){return{id:field('env_profile_id').value,name:field('env_profile_name').value,temp_normal_min:field('temp_normal_min').value,temp_normal_max:field('temp_normal_max').value,temp_alarm_min:field('temp_alarm_min').value,temp_alarm_max:field('temp_alarm_max').value,humi_normal_min:field('humi_normal_min').value,humi_normal_max:field('humi_normal_max').value,humi_alarm_min:field('humi_alarm_min').value,humi_alarm_max:field('humi_alarm_max').value};}";
    body += "var envSelect=field('env_profile_select');if(envSelect){envSelect.onchange=function(){fillEnvProfile(selectedEnvProfile());setEnvMode('edit');setEnvNotice('');};}var envNewBtn=field('envNewBtn');if(envNewBtn){envNewBtn.onclick=function(){var p=selectedEnvProfile();if(p)fillEnvProfile(p);field('env_profile_id').value='';field('env_profile_name').value='';setEnvMode('create');setEnvNotice('');};}";
    body += "var envApplyBtn=field('envApplyBtn');if(envApplyBtn){envApplyBtn.onclick=async function(){if(envEditMode==='create'){setEnvNotice(envMsg('\\u8bf7\\u5148\\u4fdd\\u5b58\\u6216\\u53d6\\u6d88\\u65b0\\u589e\\u9884\\u8bbe','Please save or cancel the new profile first'));return;}try{var text=await postForm('/api/env/profile/select',{id:field('env_profile_select').value});var res=JSON.parse(text);setEnvNotice(envMsg('\\u5df2\\u5e94\\u7528\\u9884\\u8bbe','Profile applied'));await loadEnvProfiles(res.active_env_profile_id);refreshStatus();}catch(e){setEnvNotice(envMsg('\\u5e94\\u7528\\u5931\\u8d25\\uff1a\\u9884\\u8bbe\\u4e0d\\u5b58\\u5728','Apply failed: profile not found'));}};}";
    body += "var envSaveBtn=field('envSaveBtn');if(envSaveBtn){envSaveBtn.onclick=async function(){if(envEditMode!=='edit'||!field('env_profile_id').value){setEnvNotice(envMsg('\\u8bf7\\u5148\\u9009\\u62e9\\u8981\\u7f16\\u8f91\\u7684\\u9884\\u8bbe','Please select a profile to edit'));return;}try{var data=currentEnvProfilePayload();if(!String(data.name||'').trim()){setEnvNotice(envMsg('\\u8bf7\\u586b\\u5199\\u9884\\u8bbe\\u540d\\u79f0','Please enter a profile name'));return;}var text=await postForm('/api/env/profile/save',data);var res=JSON.parse(text);setEnvNotice(envMsg('\\u5df2\\u4fdd\\u5b58\\u9884\\u8bbe','Profile saved'));await loadEnvProfiles(res.profile?res.profile.id:data.id);if(res.profile)fillEnvProfile(res.profile);}catch(e){setEnvNotice(envMsg('\\u4fdd\\u5b58\\u5931\\u8d25\\uff1a\\u9608\\u503c\\u987a\\u5e8f\\u4e0d\\u5408\\u6cd5','Save failed'));}};}";
    body += "var envSaveNewBtn=field('envSaveNewBtn');if(envSaveNewBtn){envSaveNewBtn.onclick=async function(){if(envEditMode!=='create')return;try{var data=currentEnvProfilePayload();data.id='';if(!String(data.name||'').trim()){setEnvNotice(envMsg('\\u8bf7\\u586b\\u5199\\u9884\\u8bbe\\u540d\\u79f0','Please enter a profile name'));return;}var text=await postForm('/api/env/profile/save',data);var res=JSON.parse(text);setEnvNotice(envMsg('\\u5df2\\u521b\\u5efa\\u65b0\\u9884\\u8bbe','New profile created'));await loadEnvProfiles(res.profile?res.profile.id:'');if(res.profile)fillEnvProfile(res.profile);setEnvMode('edit');}catch(e){setEnvNotice(envMsg('\\u4fdd\\u5b58\\u5931\\u8d25\\uff1a\\u9608\\u503c\\u987a\\u5e8f\\u4e0d\\u5408\\u6cd5','Save failed'));}};}";
    body += "var envCancelNewBtn=field('envCancelNewBtn');if(envCancelNewBtn){envCancelNewBtn.onclick=function(){fillEnvProfile(selectedEnvProfile());setEnvMode('edit');setEnvNotice('');};}var envDeleteBtn=field('envDeleteBtn');if(envDeleteBtn){envDeleteBtn.onclick=async function(){if(envEditMode!=='edit')return;if(!confirm(envMsg('\\u786e\\u5b9a\\u5220\\u9664\\u8fd9\\u4e2a\\u73af\\u5883\\u9884\\u8bbe\\u5417\\uff1f\\u6b64\\u64cd\\u4f5c\\u4e0d\\u53ef\\u64a4\\u9500\\u3002','Delete this environment profile? This action cannot be undone.')))return;try{var deleting=field('env_profile_select').value;var text=await postForm('/api/env/profile/delete',{id:deleting});var res=JSON.parse(text);setEnvNotice(envMsg('\\u5df2\\u5220\\u9664\\u9884\\u8bbe','Profile deleted'));await loadEnvProfiles(res.active_env_profile_id);refreshStatus();}catch(e){var msg=apiMessage(e,'');setEnvNotice(msg.indexOf('last')>=0?envMsg('\\u5220\\u9664\\u5931\\u8d25\\uff1a\\u4e0d\\u80fd\\u5220\\u9664\\u6700\\u540e\\u4e00\\u4e2a\\u9884\\u8bbe','Cannot delete the last profile'):envMsg('\\u5220\\u9664\\u5931\\u8d25','Delete failed'));}};}";
    body += "function alarmTime(e){if(e.has_real_time&&e.start_ts){return new Date(e.start_ts*1000).toLocaleString();}return lang()==='zh'?('\\u542f\\u52a8\\u540e '+Math.floor((e.start_ms||0)/1000)+' \\u79d2'):('after boot '+Math.floor((e.start_ms||0)/1000)+'s');}function alarmDuration(e){var ms=e.duration_ms||0;if(e.active)ms=Math.max(0,millisFallback()-e.start_ms);return Math.floor(ms/1000)+'s';}function millisFallback(){return Date.now()%4294967295;}function alarmStatus(e){return e.active?(lang()==='zh'?'\\u8fdb\\u884c\\u4e2d':'active'):(lang()==='zh'?'\\u5df2\\u7ed3\\u675f':'ended');}";
    body += "function renderAlarms(d){setText('alarm_summary',d.has_active_alarm?(lang()==='zh'?'\\u5f53\\u524d\\u62a5\\u8b66':'Active alarm'):(d.has_unack_alarm?(lang()==='zh'?('\\u6709 '+d.unack_alarm_count+' \\u6761\\u672a\\u786e\\u8ba4\\u62a5\\u8b66'):(d.unack_alarm_count+' unacknowledged alarm(s)')):(lang()==='zh'?'\\u5f53\\u524d\\u65e0\\u62a5\\u8b66':'No active alarm')));var rows=field('alarm_rows');if(!rows)return;rows.innerHTML='';var ev=d.events||[];if(!ev.length){rows.innerHTML='<tr><td colspan=\"8\">--</td></tr>';return;}ev.forEach(function(e){var tr=document.createElement('tr');tr.innerHTML='<td class=\"'+(e.active?'alarm-active':'alarm-ok')+'\">'+alarmStatus(e)+'</td><td>'+dash(e.message||e.reason)+'</td><td>'+alarmTime(e)+'</td><td>'+alarmDuration(e)+'</td><td>'+dash(e.max_temperature)+'</td><td>'+dash(e.max_humidity)+'</td><td>'+(e.acknowledged?(lang()==='zh'?'\\u5df2\\u786e\\u8ba4':'yes'):(lang()==='zh'?'\\u672a\\u786e\\u8ba4':'no'))+'</td><td>'+(e.acknowledged?'':'<button type=\"button\" data-alarm-id=\"'+e.id+'\">'+(lang()==='zh'?'\\u786e\\u8ba4':'Ack')+'</button>')+'</td>';rows.appendChild(tr);});rows.querySelectorAll('[data-alarm-id]').forEach(function(b){b.onclick=async function(){await postForm('/api/alarms/ack',{id:b.getAttribute('data-alarm-id')});setText('alarm_notice',lang()==='zh'?'\\u5df2\\u786e\\u8ba4':'Acknowledged');refreshAlarms();refreshStatus();};});}";
    body += "async function refreshAlarms(){try{var r=await fetch('/api/alarms',{cache:'no-store'});if(!r.ok)throw new Error('bad alarms');renderAlarms(await r.json());}catch(e){setText('alarm_summary',lang()==='zh'?'\\u62a5\\u8b66\\u8bb0\\u5f55\\u6682\\u65f6\\u65e0\\u6cd5\\u8bfb\\u53d6':'Alarm records unavailable');}}var ackAllBtn=field('ackAllAlarmsBtn');if(ackAllBtn){ackAllBtn.onclick=async function(){await fetch('/api/alarms/ack-all',{method:'POST'});setText('alarm_notice',lang()==='zh'?'\\u5df2\\u786e\\u8ba4\\u5168\\u90e8\\u62a5\\u8b66':'All alarms acknowledged');refreshAlarms();refreshStatus();};}";
    body += "function chartXValue(s,useReal){return useReal?s.sample_ts:s.sample_ms;}";
    body += "var chartState={samples:[],alarms:[],useRealTime:false,timeMode:'uptime',serverUptimeMs:0,clientFetchMs:0,plot:null,hoverIndex:-1};function chartTimeMode(data,samples){var real=samples.filter(function(s){return s.has_real_time&&s.sample_ts>0;}).length;if(samples.length&&real>=(samples.length/2))return 'device';if(data&&data.server_uptime_ms!==undefined&&data.server_uptime_ms!==null)return 'estimated';return 'uptime';}function estimatedDateMs(sample,meta){if(!meta||!meta.serverUptimeMs||!meta.clientFetchMs||sample.sample_ms===undefined||sample.sample_ms===null)return null;return Number(meta.clientFetchMs)-(Number(meta.serverUptimeMs)-Number(sample.sample_ms));}function uptimeText(ms){var sec=Math.floor((ms||0)/1000),m=Math.floor(sec/60),s=sec%60;return lang()==='zh'?('\\u542f\\u52a8\\u540e '+m+'\\u5206'+s+'\\u79d2'):('after boot '+m+'m'+s+'s');}function formatSampleTime(sample,meta){if(sample.has_real_time&&sample.sample_ts>0)return new Date(sample.sample_ts*1000).toLocaleString();var est=estimatedDateMs(sample,meta);if(est!==null)return new Date(est).toLocaleString();return uptimeText(sample.sample_ms);}function sampleTimeSourceText(mode){if(mode==='device')return lang()==='zh'?'\\u8bbe\\u5907\\u65f6\\u95f4':'device time';if(mode==='estimated')return lang()==='zh'?'\\u6d4f\\u89c8\\u5668\\u4f30\\u7b97':'browser estimate';return lang()==='zh'?'\\u542f\\u52a8\\u540e\\u65f6\\u95f4':'uptime';}";
    body += "function chartXLabel(v){if(chartState.timeMode==='device'){var d=new Date(v*1000);return String(d.getHours()).padStart(2,'0')+':'+String(d.getMinutes()).padStart(2,'0')+':'+String(d.getSeconds()).padStart(2,'0');}if(chartState.timeMode==='estimated'){var sm={sample_ms:v};var est=estimatedDateMs(sm,chartState);if(est!==null){var ed=new Date(est);return String(ed.getHours()).padStart(2,'0')+':'+String(ed.getMinutes()).padStart(2,'0')+':'+String(ed.getSeconds()).padStart(2,'0');}}return uptimeText(v);}function envText(v){if(lang()!=='zh')return v||'--';return v==='alarm'?'\\u62a5\\u8b66':(v==='warning'?'\\u6ce8\\u610f':(v==='normal'?'\\u9002\\u5b9c':dash(v)));}function alarmText(s){if(s.alarm_active)return lang()==='zh'?'\\u5f53\\u524d\\u62a5\\u8b66':'active alarm';if(s.has_unack_alarm)return lang()==='zh'?'\\u5386\\u53f2\\u672a\\u786e\\u8ba4':'past unack';return lang()==='zh'?'\\u65e0':'none';}";
    body += "function renderHistory(data){var samples=data.samples||[],mode=chartTimeMode(data,samples),meta={serverUptimeMs:data.server_uptime_ms,clientFetchMs:chartState.clientFetchMs};setText('history_summary',(lang()==='zh'?'\\u5df2\\u7f13\\u5b58 ':'Cached ')+data.count+(lang()==='zh'?' \\u6761\\uff0c\\u65f6\\u95f4\\u6765\\u6e90\\uff1a':' samples, time source: ')+sampleTimeSourceText(mode));var rows=field('history_rows');if(!rows)return;rows.innerHTML='';if(!samples.length){rows.innerHTML='<tr><td colspan=\"5\">--</td></tr>';return;}samples.slice(-10).forEach(function(s){var tr=document.createElement('tr');tr.innerHTML='<td>'+formatSampleTime(s,meta)+'</td><td>'+dash(s.temperature)+'</td><td>'+dash(s.humidity)+'</td><td>'+dash(s.env_level)+'</td><td>'+(s.alarm_active?'ALARM':(s.has_unack_alarm?'PAST':'--'))+'</td>';rows.appendChild(tr);});}async function refreshHistory(){return refreshHistoryChart();}";
    body += "function drawChartTooltip(ctx,s){if(!chartState.plot||chartState.hoverIndex<0)return;var canvas=field('history_chart'),tip=field('history_chart_tooltip');var p=chartState.plot,x=s._x;ctx.strokeStyle='rgba(0,0,0,0.35)';ctx.lineWidth=1;ctx.beginPath();ctx.moveTo(x,p.top);ctx.lineTo(x,p.top+p.height);ctx.stroke();function dot(y,c){if(y===undefined)return;ctx.fillStyle=c;ctx.beginPath();ctx.arc(x,y,4,0,Math.PI*2);ctx.fill();ctx.strokeStyle='#fff';ctx.lineWidth=1;ctx.stroke();}if(field('chart_show_temp').checked)dot(s._tempY,'#d35400');if(field('chart_show_humi').checked)dot(s._humiY,'#1f77b4');if(tip&&canvas){tip.innerHTML='<b>'+formatSampleTime(s,chartState)+'</b><br>'+((lang()==='zh')?'\\u6e29\\u5ea6':'Temp')+': '+Number(s.temperature).toFixed(1)+' C<br>'+((lang()==='zh')?'\\u6e7f\\u5ea6':'Humi')+': '+Number(s.humidity).toFixed(1)+' %<br>'+((lang()==='zh')?'\\u73af\\u5883\\u72b6\\u6001':'Env')+': '+envText(s.env_level)+'<br>'+((lang()==='zh')?'\\u62a5\\u8b66\\u72b6\\u6001':'Alarm')+': '+alarmText(s);var left=Math.min(Math.max(8,x+12),canvas.clientWidth-230),top=Math.min(Math.max(8,Math.min(s._tempY||999,s._humiY||999)-20),canvas.clientHeight-110);tip.style.left=left+'px';tip.style.top=top+'px';tip.style.display='block';}}";
    body += "function drawHistoryChart(){var canvas=field('history_chart');if(!canvas)return;var samples=chartState.samples.filter(function(s){return s.temperature!==null&&s.humidity!==null;});var wrap=canvas.parentElement,w=Math.max(320,wrap.clientWidth||canvas.clientWidth||640),h=300,dpr=window.devicePixelRatio||1;canvas.width=w*dpr;canvas.height=h*dpr;canvas.style.width='100%';canvas.style.height=h+'px';var ctx=canvas.getContext('2d');ctx.setTransform(dpr,0,0,dpr,0,0);ctx.clearRect(0,0,w,h);ctx.fillStyle='#fff';ctx.fillRect(0,0,w,h);if(!samples.length){setText('history_chart_notice',lang()==='zh'?'\\u6682\\u65e0\\u5386\\u53f2\\u6570\\u636e':'No history data');return;}if(samples.length<2){setText('history_chart_notice',lang()==='zh'?'\\u6570\\u636e\\u4e0d\\u8db3\\uff0c\\u7b49\\u5f85\\u66f4\\u591a\\u91c7\\u6837':'Not enough data; waiting for more samples');return;}chartState.useRealTime=chartState.timeMode==='device';var xs=samples.map(function(s){return chartXValue(s,chartState.useRealTime);}),minX=Math.min.apply(null,xs),maxX=Math.max.apply(null,xs);if(maxX<=minX)maxX=minX+1;var vals=[];if(field('chart_show_temp').checked)samples.forEach(function(s){vals.push(Number(s.temperature));});if(field('chart_show_humi').checked)samples.forEach(function(s){vals.push(Number(s.humidity));});if(!vals.length)vals=samples.map(function(s){return Number(s.temperature);});var minY=Math.min.apply(null,vals),maxY=Math.max.apply(null,vals);if(maxY<=minY)maxY=minY+1;var pad=(maxY-minY)*0.12;minY-=pad;maxY+=pad;var L=56,R=64,T=24,B=44,gw=w-L-R,gh=h-T-B;function xmap(x){return L+(x-minX)/(maxX-minX)*gw;}function ymap(y){return T+gh-(y-minY)/(maxY-minY)*gh;}chartState.plot={left:L,right:R,top:T,bottom:B,width:gw,height:gh,xMin:minX,xMax:maxX,yMin:minY,yMax:maxY,useRealTime:chartState.useRealTime};ctx.strokeStyle='#ddd';ctx.lineWidth=1;ctx.beginPath();ctx.moveTo(L,T);ctx.lineTo(L,T+gh);ctx.lineTo(L+gw,T+gh);ctx.stroke();ctx.fillStyle='#666';ctx.font='12px Arial';ctx.textAlign='right';for(var i=0;i<=4;i++){var y=T+gh*i/4,v=maxY-(maxY-minY)*i/4;ctx.strokeStyle='#f0f0f0';ctx.beginPath();ctx.moveTo(L,y);ctx.lineTo(L+gw,y);ctx.stroke();ctx.fillText(v.toFixed(1),L-6,y+4);}ctx.fillStyle='#666';ctx.textAlign='left';ctx.fillText(chartXLabel(minX),L,T+gh+28);ctx.textAlign='right';ctx.fillText(chartXLabel(maxX),L+gw,T+gh+28);ctx.textAlign='left';";
    body += "chartState.alarms.forEach(function(e){var aStart=chartState.useRealTime?(e.has_real_time?e.start_ts:null):e.start_ms;var aEnd=chartState.useRealTime?(e.has_real_time?(e.end_ts||maxX):null):(e.active?maxX:e.end_ms);if(aStart===null||aStart===undefined)return;if(!aEnd||aEnd<aStart)aEnd=maxX;if(aEnd<minX||aStart>maxX)return;var x1=xmap(Math.max(aStart,minX)),x2=xmap(Math.min(aEnd,maxX));ctx.fillStyle='rgba(176,0,32,0.10)';ctx.fillRect(x1,T,Math.max(2,x2-x1),gh);ctx.fillStyle='rgba(176,0,32,0.75)';ctx.textAlign='left';ctx.fillText('ALARM',x1+3,T+14);});function drawLine(key,color,yName){ctx.strokeStyle=color;ctx.lineWidth=2;ctx.beginPath();samples.forEach(function(s,i){var x=xmap(chartXValue(s,chartState.useRealTime)),y=ymap(Number(s[key]));s._x=x;s[yName]=y;if(i===0)ctx.moveTo(x,y);else ctx.lineTo(x,y);});ctx.stroke();}if(field('chart_show_temp').checked)drawLine('temperature','#d35400','_tempY');if(field('chart_show_humi').checked)drawLine('humidity','#1f77b4','_humiY');if(chartState.hoverIndex>=0&&samples[chartState.hoverIndex])drawChartTooltip(ctx,samples[chartState.hoverIndex]);setText('history_chart_notice',(lang()==='zh'?'\\u5df2\\u52a0\\u8f7d ':'Loaded ')+samples.length+(lang()==='zh'?' \\u6761\\uff0c\\u65f6\\u95f4\\u6765\\u6e90\\uff1a':' samples, time source: ')+sampleTimeSourceText(chartState.timeMode));}";
    body += "function findNearestSampleByX(mx){var best=-1,dist=1e9;chartState.samples.forEach(function(s,i){if(s._x===undefined)return;var d=Math.abs(s._x-mx);if(d<dist){dist=d;best=i;}});return best;}function chartPointerX(ev){var canvas=field('history_chart'),rect=canvas.getBoundingClientRect(),p=ev.touches?ev.touches[0]:ev;return p.clientX-rect.left;}function attachChartEvents(){var canvas=field('history_chart'),tip=field('history_chart_tooltip');if(!canvas||canvas._eventsAttached)return;canvas._eventsAttached=true;canvas.addEventListener('mousemove',function(ev){chartState.hoverIndex=findNearestSampleByX(chartPointerX(ev));drawHistoryChart();});canvas.addEventListener('mouseleave',function(){chartState.hoverIndex=-1;if(tip)tip.style.display='none';drawHistoryChart();});canvas.addEventListener('click',function(ev){chartState.hoverIndex=findNearestSampleByX(chartPointerX(ev));drawHistoryChart();});canvas.addEventListener('touchmove',function(ev){ev.preventDefault();chartState.hoverIndex=findNearestSampleByX(chartPointerX(ev));drawHistoryChart();},{passive:false});}";
    body += "async function refreshHistoryChart(){try{var hr=await fetch('/api/history?limit=120',{cache:'no-store'});if(!hr.ok)throw new Error('history');var hd=await hr.json(),ad={events:[]};chartState.clientFetchMs=Date.now();try{var ar=await fetch('/api/alarms',{cache:'no-store'});if(ar.ok)ad=await ar.json();}catch(e){}chartState.samples=(hd.samples||[]);chartState.alarms=(ad.events||[]);chartState.serverUptimeMs=hd.server_uptime_ms||0;chartState.timeMode=chartTimeMode(hd,chartState.samples);chartState.hoverIndex=-1;renderHistory(hd);drawHistoryChart();attachChartEvents();}catch(e){setText('history_chart_notice',lang()==='zh'?'\\u5386\\u53f2\\u6570\\u636e\\u8bfb\\u53d6\\u5931\\u8d25':'History data read failed');}}var refreshChartBtn=field('refreshChartBtn');if(refreshChartBtn){refreshChartBtn.onclick=refreshHistoryChart;}var refreshHistoryBtn=field('refreshHistoryBtn');if(refreshHistoryBtn){refreshHistoryBtn.onclick=refreshHistoryChart;}['chart_show_temp','chart_show_humi'].forEach(function(id){var el=field(id);if(el)el.onchange=function(){drawHistoryChart();};});";
    body += "['sensor','report','display'].forEach(function(p){['h','m','s'].forEach(function(k){var el=document.getElementById(p+'_'+k);if(el){el.addEventListener('blur',function(){updateIntervalPreview();});el.addEventListener('keydown',function(ev){if(ev.key==='Enter'){ev.preventDefault();updateIntervalPreview();}});}});});document.querySelectorAll('input').forEach(function(el){el.addEventListener('keydown',function(ev){if(ev.key==='Enter'){ev.preventDefault();updateIntervalPreview();}});});var form=document.querySelector('form[action=\"/save-mqtt\"]');if(form){form.addEventListener('submit',function(ev){if(!allowSubmit){ev.preventDefault();updateIntervalPreview();}});}var saveBtn=document.getElementById('saveBtn');if(saveBtn&&form){saveBtn.onclick=function(){updateIntervalPreview();allowSubmit=true;form.submit();};}loadEnvProfiles();updateIntervalPreview();refreshStatus();refreshAlarms();setInterval(refreshStatus,2000);setInterval(refreshAlarms,5000);";
    body += "</script>";
    server.send(200, HTML_CONTENT_TYPE, page_shell("ESP32-S3 E-Ink Terminal", body));
}

static void handle_get_config() {
    server.send(200, JSON_CONTENT_TYPE, config_to_json(config_get()));
}

static void handle_get_status() {
    server.send(200, JSON_CONTENT_TYPE, status_to_json());
}

static void handle_get_alarms() {
    server.send(200, JSON_CONTENT_TYPE, alarm_manager_events_json());
}

static void handle_get_history() {
    size_t limit = 120;
    if (server.hasArg("limit")) {
        limit = static_cast<size_t>(server.arg("limit").toInt());
    }
    if (limit == 0 || limit > MAX_HISTORY_SAMPLES) {
        limit = MAX_HISTORY_SAMPLES;
    }
    server.send(200, JSON_CONTENT_TYPE, history_manager_json(limit));
}

static void handle_alarm_ack() {
    uint32_t id = server.arg("id").toInt();
    if (id == 0 || !alarm_manager_ack(id)) {
        server.send(404, JSON_CONTENT_TYPE, "{\"ok\":false,\"message\":\"alarm not found\"}");
        return;
    }
    server.send(200, JSON_CONTENT_TYPE, "{\"ok\":true,\"message\":\"alarm acknowledged\"}");
}

static void handle_alarm_ack_all() {
    alarm_manager_ack_all();
    server.send(200, JSON_CONTENT_TYPE, "{\"ok\":true,\"message\":\"alarms acknowledged\"}");
}

static void handle_wifi_list() {
    server.send(200, JSON_CONTENT_TYPE, network_wifi_profiles_json());
}

static bool wifi_profile_exists(const String& ssid, WifiProfile* found = nullptr) {
    WifiProfile profiles[WIFI_PROFILE_MAX];
    size_t count = wifi_profiles_get_all(profiles, WIFI_PROFILE_MAX);
    for (size_t i = 0; i < count; ++i) {
        if (profiles[i].ssid == ssid) {
            if (found) *found = profiles[i];
            return true;
        }
    }
    return false;
}

static void handle_wifi_profile_save() {
    String body = server.hasArg("plain") ? server.arg("plain") : "";
    String ssid = get_arg_or_json(body, "ssid", "");
    String password = get_arg_or_json(body, "password", "");
    int priority = static_cast<int>(get_u32_arg_or_json(body, "priority", 50));
    bool enabled = get_bool_arg_or_json(body, "enabled", true);

    ssid.trim();
    if (ssid.length() == 0) {
        server.send(400, JSON_CONTENT_TYPE, "{\"ok\":false,\"message\":\"ssid required\"}");
        return;
    }
    if (priority < 0) priority = 0;
    if (priority > 100) priority = 100;

    bool exists = wifi_profile_exists(ssid);
    bool password_provided = password.length() > 0;
    if (!exists && !password_provided) {
        server.send(400, JSON_CONTENT_TYPE, "{\"ok\":false,\"message\":\"password required for new wifi profile\"}");
        return;
    }

    if (!network_wifi_profile_save(ssid, password, password_provided, priority, enabled)) {
        server.send(400, JSON_CONTENT_TYPE, "{\"ok\":false,\"message\":\"wifi profile save failed or full\"}");
        return;
    }

    server.send(200, JSON_CONTENT_TYPE, "{\"ok\":true,\"msg\":\"wifi profile saved\",\"message\":\"wifi profile saved\"}");
}

static void handle_wifi_add() {
    handle_wifi_profile_save();
}

static void handle_wifi_profile_delete() {
    String body = server.hasArg("plain") ? server.arg("plain") : "";
    String ssid = get_arg_or_json(body, "ssid", "");
    ssid.trim();

    if (ssid.length() == 0) {
        server.send(400, JSON_CONTENT_TYPE, "{\"ok\":false,\"message\":\"ssid required\"}");
        return;
    }

    if (!wifi_profile_exists(ssid)) {
        server.send(404, JSON_CONTENT_TYPE, "{\"ok\":false,\"message\":\"wifi profile not found\"}");
        return;
    }

    if (wifi_profiles_count() <= 1) {
        server.send(400, JSON_CONTENT_TYPE, "{\"ok\":false,\"msg\":\"cannot delete the last wifi profile\",\"message\":\"cannot delete the last wifi profile\"}");
        return;
    }

    bool deleting_current = network_is_wifi_connected() && WiFi.SSID() == ssid;
    if (!network_wifi_profile_delete(ssid)) {
        server.send(404, JSON_CONTENT_TYPE, "{\"ok\":false,\"message\":\"wifi profile not found\"}");
        return;
    }

    String json = "{\"ok\":true,\"msg\":\"";
    json += deleting_current ? "current wifi profile deleted, reconnect may fail after reboot" : "wifi profile deleted";
    json += "\",\"message\":\"";
    json += deleting_current ? "current wifi profile deleted, reconnect may fail after reboot" : "wifi profile deleted";
    json += "\"}";
    server.send(200, JSON_CONTENT_TYPE, json);
}

static void handle_wifi_delete() {
    handle_wifi_profile_delete();
}

static void handle_wifi_reconnect() {
    network_request_wifi_reconnect();
    server.send(200, JSON_CONTENT_TYPE, "{\"ok\":true,\"msg\":\"wifi reconnect requested\",\"message\":\"wifi reconnect requested\"}");
}

static void handle_env_profiles() {
    server.send(200, JSON_CONTENT_TYPE, config_env_profiles_json());
}

static bool env_profile_from_request(const String& body, EnvProfile& profile) {
    profile.id = get_arg_or_json(body, "id", get_arg_or_json(body, "profile_id", ""));
    profile.name = get_arg_or_json(body, "name", get_arg_or_json(body, "env_profile_name", ""));
    profile.temp_normal_min = get_float_arg_or_json(body, "temp_normal_min", NAN);
    profile.temp_normal_max = get_float_arg_or_json(body, "temp_normal_max", NAN);
    profile.temp_alarm_min = get_float_arg_or_json(body, "temp_alarm_min", NAN);
    profile.temp_alarm_max = get_float_arg_or_json(body, "temp_alarm_max", NAN);
    profile.humi_normal_min = get_float_arg_or_json(body, "humi_normal_min", NAN);
    profile.humi_normal_max = get_float_arg_or_json(body, "humi_normal_max", NAN);
    profile.humi_alarm_min = get_float_arg_or_json(body, "humi_alarm_min", NAN);
    profile.humi_alarm_max = get_float_arg_or_json(body, "humi_alarm_max", NAN);
    return !isnan(profile.temp_normal_min) && !isnan(profile.temp_normal_max) &&
           !isnan(profile.temp_alarm_min) && !isnan(profile.temp_alarm_max) &&
           !isnan(profile.humi_normal_min) && !isnan(profile.humi_normal_max) &&
           !isnan(profile.humi_alarm_min) && !isnan(profile.humi_alarm_max);
}

static String env_profile_json(const EnvProfile& p) {
    String json = "{\"id\":\"" + json_escape(p.id) + "\",";
    json += "\"name\":\"" + json_escape(p.name) + "\",";
    json += "\"temp_normal_min\":" + String(p.temp_normal_min, 1) + ",";
    json += "\"temp_normal_max\":" + String(p.temp_normal_max, 1) + ",";
    json += "\"temp_alarm_min\":" + String(p.temp_alarm_min, 1) + ",";
    json += "\"temp_alarm_max\":" + String(p.temp_alarm_max, 1) + ",";
    json += "\"humi_normal_min\":" + String(p.humi_normal_min, 1) + ",";
    json += "\"humi_normal_max\":" + String(p.humi_normal_max, 1) + ",";
    json += "\"humi_alarm_min\":" + String(p.humi_alarm_min, 1) + ",";
    json += "\"humi_alarm_max\":" + String(p.humi_alarm_max, 1) + "}";
    return json;
}

static String env_profile_response_json(const char* message, const EnvProfile* profile = nullptr) {
    EnvProfile active;
    config_env_profile_get_active(active);
    AppConfig cfg = config_get();
    String json = "{\"ok\":true,\"message\":\"";
    json += message;
    json += "\",\"active_env_profile_id\":\"" + json_escape(active.id) + "\",";
    json += "\"active_env_profile_name\":\"" + json_escape(active.name) + "\",";
    json += "\"env_profile_count\":" + String(cfg.env_profile_count);
    if (profile) {
        json += ",\"profile\":" + env_profile_json(*profile);
    }
    json += "}";
    return json;
}

static void handle_env_profile_save() {
    String body = server.hasArg("plain") ? server.arg("plain") : "";
    EnvProfile profile;
    if (!env_profile_from_request(body, profile)) {
        server.send(400, JSON_CONTENT_TYPE, "{\"ok\":false,\"message\":\"missing threshold\"}");
        return;
    }
    profile.name.trim();
    if (profile.name.length() == 0) {
        server.send(400, JSON_CONTENT_TYPE, "{\"ok\":false,\"message\":\"profile name required\"}");
        return;
    }
    EnvProfile saved;
    if (!config_env_profile_save(profile, &saved)) {
        server.send(400, JSON_CONTENT_TYPE, "{\"ok\":false,\"message\":\"env profile save failed\"}");
        return;
    }
    server.send(200, JSON_CONTENT_TYPE, env_profile_response_json("env profile saved", &saved));
}

static void handle_env_profile_select() {
    String body = server.hasArg("plain") ? server.arg("plain") : "";
    String id = get_arg_or_json(body, "id", get_arg_or_json(body, "profile_id", ""));
    if (!config_env_profile_select(id)) {
        server.send(404, JSON_CONTENT_TYPE, "{\"ok\":false,\"message\":\"env profile not found\"}");
        return;
    }
    server.send(200, JSON_CONTENT_TYPE, env_profile_response_json("env profile selected"));
}

static void handle_env_profile_delete() {
    String body = server.hasArg("plain") ? server.arg("plain") : "";
    String id = get_arg_or_json(body, "id", get_arg_or_json(body, "profile_id", ""));
    if (!config_env_profile_delete(id)) {
        server.send(400, JSON_CONTENT_TYPE, "{\"ok\":false,\"message\":\"cannot delete env profile\"}");
        return;
    }
    server.send(200, JSON_CONTENT_TYPE, env_profile_response_json("env profile deleted"));
}

static void handle_save_config() {
    AppConfig cfg = config_get();
    String body = server.hasArg("plain") ? server.arg("plain") : "";

    cfg.device_name = get_arg_or_json(body, "device_name", cfg.device_name);
    cfg.mqtt_host = get_arg_or_json(body, "mqtt_host", cfg.mqtt_host);
    cfg.mqtt_port = static_cast<uint16_t>(get_u32_arg_or_json(body, "mqtt_port", cfg.mqtt_port));
    cfg.bemfa_uid = get_arg_or_json(body, "bemfa_uid", cfg.bemfa_uid);
    cfg.mqtt_topic_temp = get_arg_or_json(body, "mqtt_topic_temp", cfg.mqtt_topic_temp);
    cfg.mqtt_topic_humi = get_arg_or_json(body, "mqtt_topic_humi", cfg.mqtt_topic_humi);
    cfg.mqtt_topic_battery = get_arg_or_json(body, "mqtt_topic_battery", cfg.mqtt_topic_battery);
    cfg.sample_interval_ms = get_u32_arg_or_json(body, "sample_interval_ms", cfg.sample_interval_ms);
    cfg.upload_interval_ms = get_u32_arg_or_json(body, "upload_interval_ms", cfg.upload_interval_ms);
    cfg.sensor_sample_interval_ms = get_u32_arg_or_json(body, "sensor_sample_interval_ms", cfg.sensor_sample_interval_ms);
    cfg.report_interval_ms = get_u32_arg_or_json(body, "report_interval_ms", cfg.report_interval_ms);
    cfg.display_refresh_interval_ms = get_u32_arg_or_json(body, "display_refresh_interval_ms", cfg.display_refresh_interval_ms);
    if ((server.hasArg("sample_interval_ms") || body.indexOf("\"sample_interval_ms\"") >= 0) &&
        !(server.hasArg("sensor_sample_interval_ms") || body.indexOf("\"sensor_sample_interval_ms\"") >= 0)) {
        cfg.sensor_sample_interval_ms = cfg.sample_interval_ms;
    }
    if ((server.hasArg("upload_interval_ms") || body.indexOf("\"upload_interval_ms\"") >= 0) &&
        !(server.hasArg("report_interval_ms") || body.indexOf("\"report_interval_ms\"") >= 0)) {
        cfg.report_interval_ms = cfg.upload_interval_ms;
    }
    cfg.battery_voltage_factor = get_float_arg_or_json(body, "battery_voltage_factor", cfg.battery_voltage_factor);
    if (!config_save(cfg)) {
        server.send(400, HTML_CONTENT_TYPE, save_result_page(false, "config save failed"));
        return;
    }
    server.send(200, JSON_CONTENT_TYPE, "{\"ok\":true,\"message\":\"config saved\"}");
}

static void handle_save_mqtt() {
    AppConfig cfg = config_get();

    cfg.device_id = server.arg("device_id");
    cfg.device_name = server.arg("device_name");
    cfg.mqtt_host = server.arg("mqtt_host");
    cfg.mqtt_port = static_cast<uint16_t>(server.arg("mqtt_port").toInt());
    cfg.mqtt_client_id = server.arg("mqtt_client_id");
    cfg.mqtt_username = server.arg("mqtt_username");
    String submitted_password = server.arg("mqtt_password");
    cfg.topic_prefix = server.arg("topic_prefix");
    cfg.sensor_sample_interval_ms = interval_ms_from_hms_form("sensor", cfg.sensor_sample_interval_ms);
    cfg.report_interval_ms = interval_ms_from_hms_form("report", cfg.report_interval_ms);
    cfg.display_refresh_interval_ms = interval_ms_from_hms_form("display", cfg.display_refresh_interval_ms);
    cfg.sample_interval_ms = cfg.sensor_sample_interval_ms;
    cfg.upload_interval_ms = cfg.report_interval_ms;

    cfg.device_id.trim();
    cfg.device_name.trim();
    cfg.mqtt_host.trim();
    cfg.mqtt_client_id.trim();
    cfg.mqtt_username.trim();
    cfg.topic_prefix.trim();

    if (cfg.mqtt_client_id.length() == 0) cfg.mqtt_client_id = cfg.device_id;
    if (cfg.topic_prefix.length() == 0) cfg.topic_prefix = "devices/" + cfg.device_id;
    if (submitted_password.length() > 0) cfg.mqtt_password = submitted_password;
    if (!config_save(cfg)) {
        server.send(400, HTML_CONTENT_TYPE, save_result_page(false, "config save failed"));
        return;
    }

    AppConfig saved_cfg = config_get();
    restart_pending = true;
    restart_at_ms = millis() + 2000;
    server.send(200, HTML_CONTENT_TYPE, save_result_page(true, "", &saved_cfg));
}

static void handle_restart() {
    server.send(200, JSON_CONTENT_TYPE, "{\"ok\":true,\"message\":\"restarting\"}");
    delay(100);
    ESP.restart();
}

static void handle_reset_config() {
    config_reset_to_default();
    server.send(200, JSON_CONTENT_TYPE, "{\"ok\":true,\"message\":\"business config reset\"}");
}

static void handle_reset_wifi() {
    server.send(200, JSON_CONTENT_TYPE, "{\"ok\":true,\"message\":\"wifi cleared, restarting for BLE provisioning\"}");
    delay(100);
    wifi_profiles_clear();
    WiFi.disconnect(true, true);
    ESP.restart();
}

void web_config_init() {
    server.on("/", HTTP_GET, handle_root);
    server.on("/save-mqtt", HTTP_POST, handle_save_mqtt);
    server.on("/api/config", HTTP_GET, handle_get_config);
    server.on("/api/status", HTTP_GET, handle_get_status);
    server.on("/api/alarms", HTTP_GET, handle_get_alarms);
    server.on("/api/history", HTTP_GET, handle_get_history);
    server.on("/api/alarms/ack", HTTP_POST, handle_alarm_ack);
    server.on("/api/alarms/ack-all", HTTP_POST, handle_alarm_ack_all);
    server.on("/api/config", HTTP_POST, handle_save_config);
    server.on("/api/wifi/profiles", HTTP_GET, handle_wifi_list);
    server.on("/api/wifi/profile/save", HTTP_POST, handle_wifi_profile_save);
    server.on("/api/wifi/profile/delete", HTTP_POST, handle_wifi_profile_delete);
    server.on("/api/wifi/reconnect", HTTP_POST, handle_wifi_reconnect);
    server.on("/api/wifi/list", HTTP_GET, handle_wifi_list);
    server.on("/api/wifi/add", HTTP_POST, handle_wifi_add);
    server.on("/api/wifi/delete", HTTP_POST, handle_wifi_delete);
    server.on("/api/env/profiles", HTTP_GET, handle_env_profiles);
    server.on("/api/env/profile/save", HTTP_POST, handle_env_profile_save);
    server.on("/api/env/profile/select", HTTP_POST, handle_env_profile_select);
    server.on("/api/env/profile/delete", HTTP_POST, handle_env_profile_delete);
    server.on("/api/restart", HTTP_POST, handle_restart);
    server.on("/api/reset-config", HTTP_POST, handle_reset_config);
    server.on("/api/reset-wifi", HTTP_POST, handle_reset_wifi);
    server.on("/favicon.ico", HTTP_GET, []() {
        server.send(204);
    });
    server.onNotFound([]() {
        server.send(404, "text/plain; charset=utf-8", "Not found");
    });
}

void web_config_begin() {
    if (server_running || server_starting || WiFi.status() != WL_CONNECTED) return;
    server_starting = true;

    String mdns_name = normalized_mdns_name();
    web_config_mdns_url = "http://" + mdns_name + ".local/";

    if (!mdns_running) {
        mdns_running = MDNS.begin(mdns_name.c_str());
        if (mdns_running) {
            if (!MDNS.addService("http", "tcp", 80)) {
                Serial.println("[Web] mDNS 服务注册失败");
            }
            Serial.print("[Web] mDNS 已启动：http://");
            Serial.print(mdns_name);
            Serial.println(".local");
        } else {
            Serial.println("[Web] mDNS 启动失败");
        }
    }

    server.begin();
    server_running = true;
    web_config_ip_url = "http://" + WiFi.localIP().toString() + "/";
    web_config_url = web_config_ip_url;
    Serial.println("[Web] 配置服务器已启动");
    Serial.print("[Web] URL：");
    Serial.println(web_config_url);
    Serial.print("[Web] mDNS URL：");
    Serial.println(web_config_mdns_url);
    server_starting = false;
}

void web_config_loop() {
    if (server_running) {
        server.handleClient();
    }
    if (restart_pending && static_cast<int32_t>(millis() - restart_at_ms) >= 0) {
        ESP.restart();
    }
}

void web_config_stop() {
    if (server_running) {
        server.stop();
        server_running = false;
    }
    if (mdns_running) {
        MDNS.end();
        mdns_running = false;
    }
}

bool web_config_is_running() {
    return server_running;
}

String web_config_get_url() {
    if (WiFi.status() == WL_CONNECTED) {
        if (web_config_mdns_url.length() == 0) {
            String mdns_name = normalized_mdns_name();
            web_config_mdns_url = "http://" + mdns_name + ".local/";
        }
        if (web_config_ip_url.length() == 0 || web_config_ip_url.indexOf("0.0.0.0") >= 0) {
            web_config_ip_url = "http://" + WiFi.localIP().toString() + "/";
        }
        web_config_url = web_config_ip_url;
        return web_config_url;
    }
    return String();
}

