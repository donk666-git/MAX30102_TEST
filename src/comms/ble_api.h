#ifndef WATCH_TEST_BLE_API_H
#define WATCH_TEST_BLE_API_H

#include <Arduino.h>

void ble_api_start();
void ble_api_stop();
void ble_api_update(bool got_sample);
bool ble_api_is_running();
bool ble_api_is_connected();
const char *ble_api_status_text();

#endif
