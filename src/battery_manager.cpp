#include "battery_manager.h"

#define ADC_CTRL_PIN 46
#define BATT_ADC_PIN 7

static float voltage_factor = 4.9f;
static float smoothed_mv = 0.0f;
static bool first_read = true;
static uint32_t last_read_ms = 0;

void battery_init() {
    pinMode(ADC_CTRL_PIN, OUTPUT);
    digitalWrite(ADC_CTRL_PIN, LOW);
    pinMode(BATT_ADC_PIN, INPUT);
}

void battery_set_voltage_factor(float factor) {
    if (factor > 1.0f && factor < 20.0f) {
        voltage_factor = factor;
    }
}

static uint32_t read_voltage_mv() {
    uint32_t now = millis();
    if (!first_read && now - last_read_ms < 100) {
        return static_cast<uint32_t>(smoothed_mv);
    }

    digitalWrite(ADC_CTRL_PIN, HIGH);
    delay(2);
    uint32_t raw_mv = static_cast<uint32_t>(analogReadMilliVolts(BATT_ADC_PIN) * voltage_factor);
    digitalWrite(ADC_CTRL_PIN, LOW);
    last_read_ms = now;

    if (first_read) {
        smoothed_mv = raw_mv;
        first_read = false;
    } else {
        smoothed_mv = smoothed_mv * 0.9f + raw_mv * 0.1f;
    }
    return static_cast<uint32_t>(smoothed_mv);
}

float get_battery_voltage() {
    return read_voltage_mv() / 1000.0f;
}

int get_battery_percent() {
    uint32_t mv = read_voltage_mv();
    if (mv >= 4100) return 100;
    if (mv < 3300) return 0;
    return static_cast<int>((mv - 3300) * 100 / (4200 - 3300));
}

bool is_battery_charging() {
    return smoothed_mv > 4050;
}
