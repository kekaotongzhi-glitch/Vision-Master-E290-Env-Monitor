#ifndef BATTERY_MANAGER_H
#define BATTERY_MANAGER_H

#include <Arduino.h>

void battery_init();
void battery_set_voltage_factor(float factor);
float get_battery_voltage();
int get_battery_percent();
bool is_battery_charging();

#endif
