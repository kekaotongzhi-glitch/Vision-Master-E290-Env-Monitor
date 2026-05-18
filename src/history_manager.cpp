#include "history_manager.h"
#include <math.h>

static const time_t HISTORY_VALID_TIME_EPOCH = 1700000000;

static HistorySample samples[MAX_HISTORY_SAMPLES];
static size_t sample_count = 0;
static size_t next_slot = 0;
static uint32_t next_seq = 1;
static uint32_t last_seq = 0;

static String json_escape(const String& value) {
    String out = value;
    out.replace("\\", "\\\\");
    out.replace("\"", "\\\"");
    return out;
}

static String json_float(float value, unsigned int decimals) {
    if (isnan(value)) return "null";
    return String(value, decimals);
}

static String sample_json(const HistorySample& sample) {
    String json = "{";
    json += "\"seq\":" + String(sample.seq) + ",";
    json += "\"sample_ms\":" + String(sample.sample_ms) + ",";
    json += "\"sample_ts\":" + String(sample.has_real_time ? sample.sample_ts : 0) + ",";
    json += "\"has_real_time\":" + String(sample.has_real_time ? "true" : "false") + ",";
    json += "\"temperature\":" + json_float(sample.temperature, 1) + ",";
    json += "\"humidity\":" + json_float(sample.humidity, 1) + ",";
    json += "\"battery_voltage\":" + json_float(sample.battery_voltage, 3) + ",";
    json += "\"battery_percent\":" + String(sample.battery_percent) + ",";
    json += "\"rssi\":" + String(sample.rssi) + ",";
    json += "\"env_level\":\"" + json_escape(sample.env_level) + "\",";
    json += "\"env_reason\":\"" + json_escape(sample.env_reason) + "\",";
    json += "\"alarm_active\":" + String(sample.alarm_active ? "true" : "false") + ",";
    json += "\"warning_active\":" + String(sample.warning_active ? "true" : "false") + ",";
    json += "\"has_active_alarm\":" + String(sample.has_active_alarm ? "true" : "false") + ",";
    json += "\"has_unack_alarm\":" + String(sample.has_unack_alarm ? "true" : "false") + ",";
    json += "\"current_alarm_event_id\":" + String(sample.current_alarm_event_id);
    json += "}";
    return json;
}

void history_manager_init() {
    sample_count = 0;
    next_slot = 0;
    next_seq = 1;
    last_seq = 0;
}

void history_manager_add_sample(float temperature,
                                float humidity,
                                float battery_voltage,
                                int battery_percent,
                                int rssi,
                                const String& env_level,
                                const String& env_reason,
                                bool alarm_active,
                                bool warning_active,
                                bool has_active_alarm,
                                bool has_unack_alarm,
                                uint32_t current_alarm_event_id,
                                unsigned long now_ms) {
    if (isnan(temperature) || isnan(humidity)) return;

    HistorySample& sample = samples[next_slot];
    sample.seq = next_seq++;
    sample.sample_ms = now_ms;
    time_t now_ts = time(nullptr);
    sample.has_real_time = now_ts > HISTORY_VALID_TIME_EPOCH;
    sample.sample_ts = sample.has_real_time ? now_ts : 0;
    sample.temperature = temperature;
    sample.humidity = humidity;
    sample.battery_voltage = battery_voltage;
    sample.battery_percent = battery_percent;
    sample.rssi = rssi;
    sample.env_level = env_level;
    sample.env_reason = env_reason;
    sample.alarm_active = alarm_active;
    sample.warning_active = warning_active;
    sample.has_active_alarm = has_active_alarm;
    sample.has_unack_alarm = has_unack_alarm;
    sample.current_alarm_event_id = current_alarm_event_id;

    last_seq = sample.seq;
    next_slot = (next_slot + 1) % MAX_HISTORY_SAMPLES;
    if (sample_count < MAX_HISTORY_SAMPLES) {
        sample_count++;
    }
}

String history_manager_json(size_t limit) {
    if (limit == 0 || limit > MAX_HISTORY_SAMPLES) {
        limit = MAX_HISTORY_SAMPLES;
    }
    size_t returned = sample_count < limit ? sample_count : limit;
    size_t oldest = sample_count < MAX_HISTORY_SAMPLES ? 0 : next_slot;
    size_t start_offset = sample_count - returned;

    time_t now_ts = time(nullptr);
    String json = "{";
    json += "\"count\":" + String(sample_count) + ",";
    json += "\"limit\":" + String(limit) + ",";
    json += "\"capacity\":" + String(MAX_HISTORY_SAMPLES) + ",";
    json += "\"server_uptime_ms\":" + String(millis()) + ",";
    json += "\"time_valid\":" + String(now_ts > HISTORY_VALID_TIME_EPOCH ? "true" : "false") + ",";
    json += "\"samples\":[";
    for (size_t i = 0; i < returned; ++i) {
        if (i > 0) json += ",";
        size_t idx = (oldest + start_offset + i) % MAX_HISTORY_SAMPLES;
        json += sample_json(samples[idx]);
    }
    json += "]}";
    return json;
}

size_t history_manager_count() {
    return sample_count;
}

size_t history_manager_capacity() {
    return MAX_HISTORY_SAMPLES;
}

uint32_t history_manager_last_seq() {
    return last_seq;
}
