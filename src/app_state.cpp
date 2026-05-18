#include "app_state.h"
#include <math.h>

static AppRuntimeState runtime_state = {
    false,
    NAN,
    NAN,
    0,
    0.0f,
    false,
    0,
    0,
    0,
    false,
    false,
    EnvLevel::NORMAL,
    EnvReason::NONE,
    "环境适宜",
    "Environment comfortable",
    false,
    false,
    0,
    false,
    0,
    false,
    0,
    0,
    0
};

void app_state_set_sensor_result(bool valid, float temperature, float humidity, uint32_t sample_ms) {
    runtime_state.last_sensor_sample_ms = sample_ms;
    bool new_data_valid = valid && !isnan(temperature) && !isnan(humidity);
    if (new_data_valid) {
        runtime_state.sensor_valid = true;
        runtime_state.temperature = temperature;
        runtime_state.humidity = humidity;
        runtime_state.last_valid_sensor_sample_ms = sample_ms;
    } else if (runtime_state.last_valid_sensor_sample_ms == 0) {
        runtime_state.sensor_valid = false;
    }
}

void app_state_set_battery(float voltage, int percent, bool charging) {
    runtime_state.battery_voltage = voltage;
    runtime_state.battery_percent = percent;
    runtime_state.charging = charging;
}

void app_state_set_last_mqtt_upload(uint32_t upload_ms) {
    runtime_state.last_mqtt_upload_ms = upload_ms;
}

void app_state_set_pending(bool report_pending, bool display_pending) {
    runtime_state.report_pending = report_pending;
    runtime_state.display_pending = display_pending;
}

bool app_state_set_environment(const EnvStatus& status, uint32_t now_ms) {
    bool changed = runtime_state.env_level != status.level || runtime_state.env_reason != status.reason;
    runtime_state.env_level = status.level;
    runtime_state.env_reason = status.reason;
    runtime_state.env_message_cn = status.message_cn;
    runtime_state.env_message_en = status.message_en;
    runtime_state.alarm_active = status.alarm_active;
    runtime_state.warning_active = status.warning_active;
    if (changed) {
        runtime_state.last_env_change_ms = now_ms;
    }
    return changed;
}

void app_state_set_alarm_summary(bool active,
                                 uint32_t current_id,
                                 bool has_unack,
                                 uint32_t unack_count,
                                 uint32_t event_count,
                                 uint32_t last_event_id) {
    runtime_state.current_alarm_event_active = active;
    runtime_state.current_alarm_event_id = current_id;
    runtime_state.has_unack_alarm = has_unack;
    runtime_state.unack_alarm_count = unack_count;
    runtime_state.alarm_event_count = event_count;
    runtime_state.last_alarm_event_id = last_event_id;
}

AppRuntimeState app_state_get() {
    return runtime_state;
}

uint32_t app_state_data_age_ms(uint32_t now_ms) {
    if (runtime_state.last_valid_sensor_sample_ms == 0) {
        return UINT32_MAX;
    }
    return now_ms - runtime_state.last_valid_sensor_sample_ms;
}

bool app_state_data_fresh(uint32_t now_ms, uint32_t effective_sample_interval_ms) {
    uint32_t age_ms = app_state_data_age_ms(now_ms);
    if (age_ms == UINT32_MAX) return false;
    uint32_t fresh_limit_ms = effective_sample_interval_ms + (effective_sample_interval_ms / 2);
    return age_ms <= fresh_limit_ms;
}
