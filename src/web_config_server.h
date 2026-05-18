#ifndef WEB_CONFIG_SERVER_H
#define WEB_CONFIG_SERVER_H

#include <Arduino.h>

void web_config_init();
void web_config_begin();
void web_config_loop();
void web_config_stop();
bool web_config_is_running();
String web_config_get_url();

#endif
