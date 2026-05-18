#pragma once

#include <Arduino.h>

enum class EnvLevel {
    NORMAL,
    WARNING,
    ALARM
};

enum class EnvReason {
    NONE,
    TEMP_LOW,
    TEMP_HIGH,
    HUMI_LOW,
    HUMI_HIGH,
    TEMP_HUMI_ABNORMAL
};

struct EnvStatus {
    EnvLevel level;
    EnvReason reason;
    String level_text_cn;
    String level_text_en;
    String message_cn;
    String message_en;
    bool alarm_active;
    bool warning_active;
};

void environment_monitor_init();
EnvStatus environment_evaluate(float temperature, float humidity, bool sensor_valid);
String environment_level_to_string(EnvLevel level);
String environment_reason_to_string(EnvReason reason);
