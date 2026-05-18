#pragma once

#include <Arduino.h>
#include <time.h>

static const size_t MAX_HISTORY_SAMPLES = 240;

struct HistorySample {
    uint32_t seq;

    unsigned long sample_ms;
    time_t sample_ts;
    bool has_real_time;

    float temperature;
    float humidity;
    float battery_voltage;
    int battery_percent;
    int rssi;

    String env_level;
    String env_reason;

    bool alarm_active;
    bool warning_active;
    bool has_active_alarm;
    bool has_unack_alarm;
    uint32_t current_alarm_event_id;
};

void history_manager_init();

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
                                unsigned long now_ms);

String history_manager_json(size_t limit);

size_t history_manager_count();
size_t history_manager_capacity();
uint32_t history_manager_last_seq();
