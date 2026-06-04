#ifndef MAX30205_DRIVER_H
#define MAX30205_DRIVER_H

#include <Arduino.h>

struct Max30205Reading {
    float temperature_c = 0.0f;
    float body_temperature_c = 0.0f;
    float baseline_temperature_c = 0.0f;
    float stable_sensor_temperature_c = 0.0f;
    float stability_range_c = 0.0f;
    int16_t raw = 0;
    uint32_t sample_count = 0;
    uint32_t error_count = 0;
    unsigned long last_update_ms = 0;
    unsigned long contact_elapsed_ms = 0;
    uint8_t config = 0;
    uint8_t init_error = 0;
    bool present = false;
    bool initialized = false;
    bool valid = false;
    bool contact_detected = false;
    bool body_valid = false;
};

bool max30205_begin();
bool max30205_update();
const Max30205Reading &max30205_reading();
bool max30205_present();
const char *max30205_init_error_text(uint8_t error);

#endif
