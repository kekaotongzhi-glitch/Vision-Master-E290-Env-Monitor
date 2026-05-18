#pragma once

#include <Arduino.h>
#include "environment_monitor.h"

struct AppRuntimeState {
    bool sensor_valid;
    float temperature;
    float humidity;
    int battery_percent;
    float battery_voltage;
    bool charging;
    uint32_t last_sensor_sample_ms;
    uint32_t last_valid_sensor_sample_ms;
    uint32_t last_mqtt_upload_ms;
    bool report_pending;
    bool display_pending;
    EnvLevel env_level;
    EnvReason env_reason;
    String env_message_cn;
    String env_message_en;
    bool alarm_active;
    bool warning_active;
    uint32_t last_env_change_ms;
    bool current_alarm_event_active;
    uint32_t current_alarm_event_id;
    bool has_unack_alarm;
    uint32_t unack_alarm_count;
    uint32_t alarm_event_count;
    uint32_t last_alarm_event_id;
};

void app_state_set_sensor_result(bool valid, float temperature, float humidity, uint32_t sample_ms);
void app_state_set_battery(float voltage, int percent, bool charging);
void app_state_set_last_mqtt_upload(uint32_t upload_ms);
void app_state_set_pending(bool report_pending, bool display_pending);
bool app_state_set_environment(const EnvStatus& status, uint32_t now_ms);
void app_state_set_alarm_summary(bool active,
                                 uint32_t current_id,
                                 bool has_unack,
                                 uint32_t unack_count,
                                 uint32_t event_count,
                                 uint32_t last_event_id);
AppRuntimeState app_state_get();
uint32_t app_state_data_age_ms(uint32_t now_ms);
bool app_state_data_fresh(uint32_t now_ms, uint32_t effective_sample_interval_ms);
