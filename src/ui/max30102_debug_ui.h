#ifndef MAX30102_DEBUG_UI_H
#define MAX30102_DEBUG_UI_H

#include "../drivers/max30102.h"
#include "../drivers/max30205.h"

void max30102_debug_ui_begin();
void max30102_debug_ui_push_sample(const Max30102Reading &reading, bool got_sample);
void max30102_debug_ui_update(const Max30102Reading &reading, const Max30205Reading &temperature);

#endif
