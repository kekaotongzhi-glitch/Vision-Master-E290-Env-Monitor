#include "environment_monitor.h"
#include "config_manager.h"
#include <math.h>

static EnvStatus current_status;
static EnvLevel candidate_level = EnvLevel::NORMAL;
static EnvReason candidate_reason = EnvReason::NONE;
static uint8_t abnormal_count = 0;
static uint8_t normal_count = 0;

static uint8_t level_rank(EnvLevel level) {
    switch (level) {
        case EnvLevel::ALARM: return 2;
        case EnvLevel::WARNING: return 1;
        case EnvLevel::NORMAL:
        default: return 0;
    }
}

static EnvStatus make_status(EnvLevel level, EnvReason reason) {
    EnvStatus status;
    status.level = level;
    status.reason = reason;
    status.alarm_active = level == EnvLevel::ALARM;
    status.warning_active = level == EnvLevel::WARNING;

    if (level == EnvLevel::ALARM) {
        status.level_text_cn = "报警";
        status.level_text_en = "Alarm";
    } else if (level == EnvLevel::WARNING) {
        status.level_text_cn = "注意";
        status.level_text_en = "Warning";
    } else {
        status.level_text_cn = "适宜";
        status.level_text_en = "Normal";
    }

    switch (reason) {
        case EnvReason::TEMP_LOW:
            status.message_cn = "温度过低";
            status.message_en = "Temperature too low";
            break;
        case EnvReason::TEMP_HIGH:
            status.message_cn = "温度过高";
            status.message_en = "Temperature too high";
            break;
        case EnvReason::HUMI_LOW:
            status.message_cn = "湿度过低";
            status.message_en = "Humidity too low";
            break;
        case EnvReason::HUMI_HIGH:
            status.message_cn = "湿度过高";
            status.message_en = "Humidity too high";
            break;
        case EnvReason::TEMP_HUMI_ABNORMAL:
            status.message_cn = "温湿度异常";
            status.message_en = "Temperature and humidity abnormal";
            break;
        case EnvReason::NONE:
        default:
            status.message_cn = level == EnvLevel::NORMAL ? "环境适宜" : "传感器数据异常";
            status.message_en = level == EnvLevel::NORMAL ? "Environment comfortable" : "Sensor data invalid";
            break;
    }
    return status;
}

void environment_monitor_init() {
    current_status = make_status(EnvLevel::NORMAL, EnvReason::NONE);
    candidate_level = EnvLevel::NORMAL;
    candidate_reason = EnvReason::NONE;
    abnormal_count = 0;
    normal_count = 0;
}

String environment_level_to_string(EnvLevel level) {
    switch (level) {
        case EnvLevel::ALARM: return "alarm";
        case EnvLevel::WARNING: return "warning";
        case EnvLevel::NORMAL:
        default: return "normal";
    }
}

String environment_reason_to_string(EnvReason reason) {
    switch (reason) {
        case EnvReason::TEMP_LOW: return "temp_low";
        case EnvReason::TEMP_HIGH: return "temp_high";
        case EnvReason::HUMI_LOW: return "humi_low";
        case EnvReason::HUMI_HIGH: return "humi_high";
        case EnvReason::TEMP_HUMI_ABNORMAL: return "temp_humi_abnormal";
        case EnvReason::NONE:
        default: return "none";
    }
}

static EnvStatus raw_evaluate(float temperature, float humidity, bool sensor_valid) {
    if (!sensor_valid || isnan(temperature) || isnan(humidity)) {
        return make_status(EnvLevel::WARNING, EnvReason::NONE);
    }

    AppConfig cfg = config_get();
    EnvLevel temp_level = EnvLevel::NORMAL;
    EnvReason temp_reason = EnvReason::NONE;
    EnvLevel humi_level = EnvLevel::NORMAL;
    EnvReason humi_reason = EnvReason::NONE;

    if (temperature < cfg.temp_alarm_min) {
        temp_level = EnvLevel::ALARM;
        temp_reason = EnvReason::TEMP_LOW;
    } else if (temperature > cfg.temp_alarm_max) {
        temp_level = EnvLevel::ALARM;
        temp_reason = EnvReason::TEMP_HIGH;
    } else if (temperature < cfg.temp_normal_min) {
        temp_level = EnvLevel::WARNING;
        temp_reason = EnvReason::TEMP_LOW;
    } else if (temperature > cfg.temp_normal_max) {
        temp_level = EnvLevel::WARNING;
        temp_reason = EnvReason::TEMP_HIGH;
    }

    if (humidity < cfg.humi_alarm_min) {
        humi_level = EnvLevel::ALARM;
        humi_reason = EnvReason::HUMI_LOW;
    } else if (humidity > cfg.humi_alarm_max) {
        humi_level = EnvLevel::ALARM;
        humi_reason = EnvReason::HUMI_HIGH;
    } else if (humidity < cfg.humi_normal_min) {
        humi_level = EnvLevel::WARNING;
        humi_reason = EnvReason::HUMI_LOW;
    } else if (humidity > cfg.humi_normal_max) {
        humi_level = EnvLevel::WARNING;
        humi_reason = EnvReason::HUMI_HIGH;
    }

    if (temp_level != EnvLevel::NORMAL && humi_level != EnvLevel::NORMAL) {
        return make_status(level_rank(temp_level) >= level_rank(humi_level) ? temp_level : humi_level, EnvReason::TEMP_HUMI_ABNORMAL);
    }
    if (level_rank(temp_level) >= level_rank(humi_level) && temp_level != EnvLevel::NORMAL) {
        return make_status(temp_level, temp_reason);
    }
    if (humi_level != EnvLevel::NORMAL) {
        return make_status(humi_level, humi_reason);
    }
    return make_status(EnvLevel::NORMAL, EnvReason::NONE);
}

EnvStatus environment_evaluate(float temperature, float humidity, bool sensor_valid) {
    EnvStatus raw = raw_evaluate(temperature, humidity, sensor_valid);

    if (!sensor_valid) {
        current_status = raw;
        abnormal_count = 0;
        normal_count = 0;
        return current_status;
    }

    if (raw.level == EnvLevel::NORMAL) {
        abnormal_count = 0;
        normal_count++;
        if (current_status.level != EnvLevel::NORMAL && normal_count >= 3) {
            current_status = raw;
        } else if (current_status.level == EnvLevel::NORMAL) {
            current_status = raw;
        }
        return current_status;
    }

    normal_count = 0;
    if (raw.level != candidate_level || raw.reason != candidate_reason) {
        candidate_level = raw.level;
        candidate_reason = raw.reason;
        abnormal_count = 1;
    } else {
        abnormal_count++;
    }

    if (current_status.level == EnvLevel::NORMAL || raw.level != current_status.level || raw.reason != current_status.reason) {
        if (abnormal_count >= 2) {
            current_status = raw;
        }
    } else {
        current_status = raw;
    }
    return current_status;
}
