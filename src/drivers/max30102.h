#ifndef MAX30102_DRIVER_H
#define MAX30102_DRIVER_H

#include <Arduino.h>

struct Max30102Reading {
    uint32_t red = 0;
    uint32_t ir = 0;
    float bpm = 0.0f;
    float spo2 = 0.0f;
    float ir_cardiogram = 0.0f;
    float dc_filtered_ir = 0.0f;
    float dc_filtered_red = 0.0f;
    float last_candidate_bpm = 0.0f;
    float last_accepted_bpm = 0.0f;
    float last_peak_value = 0.0f;
    float spo2_ratio = 0.0f;
    float spo2_red_amp = 0.0f;
    float spo2_ir_amp = 0.0f;
    uint32_t sample_count = 0;
    uint32_t accepted_pulses = 0;
    uint32_t rejected_pulses = 0;
    uint16_t last_candidate_ms = 0;
    uint16_t last_accepted_ms = 0;
    uint16_t samples_last_second = 0;
    uint8_t fifo_wr_ptr = 0;
    uint8_t fifo_rd_ptr = 0;
    uint8_t fifo_ovf = 0;
    uint8_t rev_id = 0;
    uint8_t part_id = 0;
    uint8_t mode_reg = 0;
    uint8_t spo2_config = 0;
    uint8_t red_led_current = 0;
    uint8_t ir_led_current = 0;
    int32_t red_ir_dc_diff = 0;
    bool present = false;
    bool initialized = false;
    bool finger_present = false;
    bool pulse_detected = false;
    bool saturated = false;
    bool settling = false;
    bool measurement_active = false;
    unsigned long active_elapsed_ms = 0;
    unsigned long active_remaining_ms = 0;
    unsigned long settle_remaining_ms = 0;
    unsigned long next_measurement_ms = 0;
    uint8_t init_error = 0;
};

bool max30102_begin();
bool max30102_update();
const Max30102Reading &max30102_reading();
bool max30102_present();
bool max30102_set_led_currents(uint8_t red_current, uint8_t ir_current);
const char *max30102_init_error_text(uint8_t error);

#endif
