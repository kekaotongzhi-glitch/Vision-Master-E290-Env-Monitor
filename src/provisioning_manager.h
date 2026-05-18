#ifndef PROVISIONING_MANAGER_H
#define PROVISIONING_MANAGER_H

#include <Arduino.h>

void provisioning_init();
void provisioning_start_ble();
void provisioning_stop();
bool provisioning_is_running();
void provisioning_loop();
String provisioning_get_service_name();
String provisioning_get_pop();
String provisioning_get_qr_payload();

#endif
