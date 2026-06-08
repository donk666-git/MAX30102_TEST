#ifndef WATCH_TEST_WEB_API_H
#define WATCH_TEST_WEB_API_H

#include <Arduino.h>

void web_api_init();
void web_api_update();
void web_api_stop();
bool web_api_is_running();
uint8_t web_api_client_count();
const char *web_api_status_text();
String web_api_ap_ip();

#endif
