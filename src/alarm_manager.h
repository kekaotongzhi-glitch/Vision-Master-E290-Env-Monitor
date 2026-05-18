#pragma once

#include <Arduino.h>
#include "environment_monitor.h"

struct AlarmEvent {
    uint32_t id;
    bool active;
    bool acknowledged;
    String level;
    String reason;
    String message;
    unsigned long start_ms;
    unsigned long end_ms;
    unsigned long duration_ms;
    time_t start_ts;
    time_t end_ts;
    bool has_real_time;
    float start_temperature;
    float start_humidity;
    float max_temperature;
    float min_temperature;
    float max_humidity;
    float min_humidity;
};

void alarm_manager_init();
void alarm_manager_on_env_update(EnvLevel level,
                                 EnvReason reason,
                                 const String& message,
                                 float temperature,
                                 float humidity,
                                 bool sensor_valid,
                                 unsigned long now_ms);

bool alarm_manager_has_active_alarm();
bool alarm_manager_has_unack_alarm();
uint32_t alarm_manager_unack_count();
uint32_t alarm_manager_event_count();
uint32_t alarm_manager_last_event_id();
uint32_t alarm_manager_current_event_id();

String alarm_manager_events_json();
String alarm_manager_status_json();

bool alarm_manager_ack(uint32_t id);
void alarm_manager_ack_all();
