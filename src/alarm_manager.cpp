#include "alarm_manager.h"
#include "app_state.h"
#include <math.h>
#include <time.h>

static const uint8_t MAX_ALARM_EVENTS = 20;
static const time_t VALID_TIME_EPOCH = 1609459200;

static AlarmEvent events[MAX_ALARM_EVENTS];
static uint8_t event_count = 0;
static uint8_t next_slot = 0;
static uint32_t next_id = 1;
static int8_t active_slot = -1;

static String json_escape_alarm(const String& value) {
    String out = value;
    out.replace("\\", "\\\\");
    out.replace("\"", "\\\"");
    return out;
}

static String json_float(float value, unsigned int decimals) {
    if (isnan(value)) return "null";
    return String(value, decimals);
}

static bool current_time_valid(time_t& out) {
    out = time(nullptr);
    return out >= VALID_TIME_EPOCH;
}

static void update_app_alarm_state() {
    app_state_set_alarm_summary(alarm_manager_has_active_alarm(),
                                alarm_manager_current_event_id(),
                                alarm_manager_has_unack_alarm(),
                                alarm_manager_unack_count(),
                                alarm_manager_event_count(),
                                alarm_manager_last_event_id());
}

static AlarmEvent& create_event(EnvReason reason,
                                const String& message,
                                float temperature,
                                float humidity,
                                unsigned long now_ms) {
    AlarmEvent& event = events[next_slot];
    event.id = next_id++;
    event.active = true;
    event.acknowledged = false;
    event.level = "alarm";
    event.reason = environment_reason_to_string(reason);
    event.message = message;
    event.start_ms = now_ms;
    event.end_ms = 0;
    event.duration_ms = 0;
    event.start_ts = 0;
    event.end_ts = 0;
    event.has_real_time = current_time_valid(event.start_ts);
    event.start_temperature = temperature;
    event.start_humidity = humidity;
    event.max_temperature = temperature;
    event.min_temperature = temperature;
    event.max_humidity = humidity;
    event.min_humidity = humidity;

    active_slot = next_slot;
    next_slot = (next_slot + 1) % MAX_ALARM_EVENTS;
    if (event_count < MAX_ALARM_EVENTS) event_count++;
    return event;
}

static void update_active_extremes(float temperature, float humidity) {
    if (active_slot < 0) return;
    AlarmEvent& event = events[active_slot];
    if (!event.active) return;
    if (!isnan(temperature)) {
        if (isnan(event.max_temperature) || temperature > event.max_temperature) event.max_temperature = temperature;
        if (isnan(event.min_temperature) || temperature < event.min_temperature) event.min_temperature = temperature;
    }
    if (!isnan(humidity)) {
        if (isnan(event.max_humidity) || humidity > event.max_humidity) event.max_humidity = humidity;
        if (isnan(event.min_humidity) || humidity < event.min_humidity) event.min_humidity = humidity;
    }
}

static void close_active_event(unsigned long now_ms) {
    if (active_slot < 0) return;
    AlarmEvent& event = events[active_slot];
    if (!event.active) {
        active_slot = -1;
        return;
    }
    event.active = false;
    event.end_ms = now_ms;
    event.duration_ms = now_ms - event.start_ms;
    time_t end_ts = 0;
    if (current_time_valid(end_ts)) {
        event.end_ts = end_ts;
        event.has_real_time = event.has_real_time || end_ts >= VALID_TIME_EPOCH;
    }
    active_slot = -1;
}

static String event_json(const AlarmEvent& event) {
    String json = "{";
    json += "\"id\":" + String(event.id) + ",";
    json += "\"active\":" + String(event.active ? "true" : "false") + ",";
    json += "\"acknowledged\":" + String(event.acknowledged ? "true" : "false") + ",";
    json += "\"level\":\"" + json_escape_alarm(event.level) + "\",";
    json += "\"reason\":\"" + json_escape_alarm(event.reason) + "\",";
    json += "\"message\":\"" + json_escape_alarm(event.message) + "\",";
    json += "\"start_ms\":" + String(event.start_ms) + ",";
    json += "\"end_ms\":" + String(event.end_ms) + ",";
    json += "\"duration_ms\":" + String(event.duration_ms) + ",";
    json += "\"has_real_time\":" + String(event.has_real_time ? "true" : "false") + ",";
    json += "\"start_ts\":" + String(static_cast<long>(event.start_ts)) + ",";
    json += "\"end_ts\":" + String(static_cast<long>(event.end_ts)) + ",";
    json += "\"start_temperature\":" + json_float(event.start_temperature, 1) + ",";
    json += "\"start_humidity\":" + json_float(event.start_humidity, 1) + ",";
    json += "\"max_temperature\":" + json_float(event.max_temperature, 1) + ",";
    json += "\"min_temperature\":" + json_float(event.min_temperature, 1) + ",";
    json += "\"max_humidity\":" + json_float(event.max_humidity, 1) + ",";
    json += "\"min_humidity\":" + json_float(event.min_humidity, 1);
    json += "}";
    return json;
}

void alarm_manager_init() {
    event_count = 0;
    next_slot = 0;
    next_id = 1;
    active_slot = -1;
    update_app_alarm_state();
}

void alarm_manager_on_env_update(EnvLevel level,
                                 EnvReason reason,
                                 const String& message,
                                 float temperature,
                                 float humidity,
                                 bool sensor_valid,
                                 unsigned long now_ms) {
    if (!sensor_valid || isnan(temperature) || isnan(humidity)) {
        update_app_alarm_state();
        return;
    }

    if (level == EnvLevel::ALARM) {
        if (active_slot < 0 || !events[active_slot].active) {
            create_event(reason, message, temperature, humidity, now_ms);
        } else {
            update_active_extremes(temperature, humidity);
            events[active_slot].reason = environment_reason_to_string(reason);
            events[active_slot].message = message;
        }
    } else if (active_slot >= 0 && events[active_slot].active) {
        close_active_event(now_ms);
    }

    update_app_alarm_state();
}

bool alarm_manager_has_active_alarm() {
    return active_slot >= 0 && events[active_slot].active;
}

bool alarm_manager_has_unack_alarm() {
    for (uint8_t i = 0; i < event_count; ++i) {
        if (!events[i].acknowledged) return true;
    }
    return false;
}

uint32_t alarm_manager_unack_count() {
    uint32_t count = 0;
    for (uint8_t i = 0; i < event_count; ++i) {
        if (!events[i].acknowledged) count++;
    }
    return count;
}

uint32_t alarm_manager_event_count() {
    return event_count;
}

uint32_t alarm_manager_last_event_id() {
    uint32_t id = 0;
    for (uint8_t i = 0; i < event_count; ++i) {
        if (events[i].id > id) id = events[i].id;
    }
    return id;
}

uint32_t alarm_manager_current_event_id() {
    if (!alarm_manager_has_active_alarm()) return 0;
    return events[active_slot].id;
}

String alarm_manager_status_json() {
    String json = "\"has_active_alarm\":";
    json += alarm_manager_has_active_alarm() ? "true" : "false";
    json += ",\"has_unack_alarm\":";
    json += alarm_manager_has_unack_alarm() ? "true" : "false";
    json += ",\"unack_alarm_count\":" + String(alarm_manager_unack_count());
    json += ",\"alarm_event_count\":" + String(alarm_manager_event_count());
    json += ",\"last_alarm_event_id\":" + String(alarm_manager_last_event_id());
    json += ",\"current_alarm_event_id\":" + String(alarm_manager_current_event_id());
    return json;
}

String alarm_manager_events_json() {
    String json = "{";
    json += alarm_manager_status_json();
    json += ",\"events\":[";
    for (uint8_t n = 0; n < event_count; ++n) {
        uint8_t idx = (next_slot + MAX_ALARM_EVENTS - 1 - n) % MAX_ALARM_EVENTS;
        if (n > 0) json += ",";
        json += event_json(events[idx]);
    }
    json += "]}";
    return json;
}

bool alarm_manager_ack(uint32_t id) {
    for (uint8_t i = 0; i < event_count; ++i) {
        if (events[i].id == id) {
            events[i].acknowledged = true;
            update_app_alarm_state();
            return true;
        }
    }
    return false;
}

void alarm_manager_ack_all() {
    for (uint8_t i = 0; i < event_count; ++i) {
        events[i].acknowledged = true;
    }
    update_app_alarm_state();
}
