#ifndef WATCH_TEST_TIME_API_H
#define WATCH_TEST_TIME_API_H

#include <Arduino.h>

void time_api_init();
bool time_api_set_epoch(long epoch);
bool time_api_is_valid();
void time_api_get_strings(char *time_buf, size_t time_size, char *date_buf, size_t date_size);
long time_api_epoch();

#endif
