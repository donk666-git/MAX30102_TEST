#include "max30102.h"
#include "i2c_bus.h"
#include "../config.h"
#include <math.h>

static constexpr uint32_t FINGER_ON_IR = 50000;
static constexpr uint32_t FINGER_OFF_IR = 30000;
static constexpr uint8_t FINGER_LOST_SAMPLES = 5;
static constexpr unsigned long BPM_HOLD_MS = 12000;
static constexpr unsigned long SPO2_HOLD_MS = 15000;
static constexpr unsigned long MEASUREMENT_PERIOD_MS = 0;
static constexpr unsigned long MEASUREMENT_ACTIVE_MS = 0;

static constexpr uint8_t REF_MEAN_FILTER_SIZE = 17;
static constexpr uint8_t REF_BPM_SAMPLE_SIZE = 15;
static constexpr uint8_t REF_RESET_SPO2_EVERY_N_PULSES = 6;
static constexpr float REF_DC_ALPHA = 0.925f;
static constexpr uint16_t REF_PULSE_MIN_THRESHOLD = 250;
static constexpr uint16_t REF_PULSE_MAX_THRESHOLD = 5000;
static constexpr float REF_VALID_BPM_MIN = 45.0f;
static constexpr uint16_t REF_DUPLICATE_PEAK_MS = 240;
static constexpr float REF_SHORT_INTERVAL_RATIO = 0.55f;
static constexpr uint32_t REF_ACCEPTABLE_INTENSITY_DIFF = 65000;
static constexpr uint32_t REF_RED_LED_ADJUSTMENT_MS = 500;

static constexpr uint8_t FIFO_CONFIG = 0x10;
static constexpr uint8_t SPO2_CONFIG = 0x67;
static constexpr uint8_t RED_LED_CURRENT = 0x3F;
static constexpr uint8_t IR_LED_CURRENT = 0x3F;
static constexpr uint8_t LED_CURRENT_MIN = 0x00;
static constexpr uint8_t LED_CURRENT_MAX = 0xFA;
static constexpr uint8_t LED_SATURATION_STEP = 0x08;
static constexpr unsigned long LED_SATURATION_ADJUSTMENT_MS = 150;
static constexpr uint32_t RAW_SATURATION_LEVEL = 250000;
static constexpr uint8_t RAW_TO_REF_SHIFT = 2;
static constexpr uint8_t MODE_SPO2 = 0x03;
static constexpr uint8_t MODE_RESET = 0x40;
static constexpr uint8_t MODE_SHDN = 0x80;

enum InitError : uint8_t {
    INIT_OK = 0,
    INIT_NOT_FOUND = 1,
    INIT_BAD_PART_ID = 2,
    INIT_RESET_WRITE_FAILED = 3,
    INIT_RESET_TIMEOUT = 4,
    INIT_CONFIG_WRITE_FAILED = 5,
    INIT_MODE_VERIFY_FAILED = 6,
    INIT_START_FAILED = 7
};

enum RefPulseState {
    REF_PULSE_IDLE,
    REF_PULSE_TRACE_UP,
    REF_PULSE_TRACE_DOWN
};

struct RefDcFilter {
    float w = 0.0f;
    float result = 0.0f;
};

struct RefLpbFilter {
    float v[2] = {0.0f, 0.0f};
    float result = 0.0f;
};

struct HealthState {
    bool finger_present = false;
    bool last_finger_present = false;
    uint8_t low_ir_samples = 0;

    float ir_dc = 0.0f;
    float red_dc = 0.0f;
    float bpm = 0.0f;
    float spo2 = 0.0f;
    unsigned long last_bpm_ms = 0;
    unsigned long last_spo2_ms = 0;

    RefPulseState pulse_state = REF_PULSE_IDLE;
    float prev_sensor_value = 0.0f;
    uint8_t values_went_down = 0;
    unsigned long current_beat = 0;
    unsigned long last_beat = 0;
    float last_beat_threshold = 0.0f;

    RefDcFilter dc_ir;
    RefDcFilter dc_red;
    RefLpbFilter lpb_ir;

    float mean_values[REF_MEAN_FILTER_SIZE] = {0.0f};
    uint8_t mean_index = 0;
    float mean_sum = 0.0f;
    uint8_t mean_count = 0;

    uint16_t beat_intervals[REF_BPM_SAMPLE_SIZE] = {0};
    uint8_t bpm_count = 0;
    uint8_t bpm_index = 0;

    float ir_ac_sq_sum = 0.0f;
    float red_ac_sq_sum = 0.0f;
    uint16_t samples_recorded = 0;
    uint16_t pulses_detected = 0;
};

static bool s_present = false;
static HealthState s_health;
static Max30102Reading s_reading;
static unsigned long s_last_rate_ms = 0;
static uint32_t s_last_rate_count = 0;
static unsigned long s_last_led_balance_ms = 0;
static unsigned long s_last_saturation_adjust_ms = 0;
static uint8_t s_red_led_current = RED_LED_CURRENT;
static uint8_t s_ir_led_current = IR_LED_CURRENT;
static bool s_measurement_active = false;
static unsigned long s_measurement_started_ms = 0;
static unsigned long s_next_measurement_ms = 0;

static void reset_signal_state(bool keep_values);

static bool write_reg_checked(uint8_t reg, uint8_t value)
{
    return i2c_write_reg8(MAX30102_ADDR, reg, value);
}

static void refresh_debug_regs()
{
    s_reading.fifo_wr_ptr = i2c_read_reg8(MAX30102_ADDR, 0x04);
    s_reading.fifo_ovf = i2c_read_reg8(MAX30102_ADDR, 0x05);
    s_reading.fifo_rd_ptr = i2c_read_reg8(MAX30102_ADDR, 0x06);
    s_reading.rev_id = i2c_read_reg8(MAX30102_ADDR, 0xFE);
    s_reading.part_id = i2c_read_reg8(MAX30102_ADDR, 0xFF);
    s_reading.mode_reg = i2c_read_reg8(MAX30102_ADDR, 0x09);
    s_reading.spo2_config = i2c_read_reg8(MAX30102_ADDR, 0x0A);
    s_reading.red_led_current = i2c_read_reg8(MAX30102_ADDR, 0x0C);
    s_reading.ir_led_current = i2c_read_reg8(MAX30102_ADDR, 0x0D);
}

static void refresh_schedule_fields(unsigned long now)
{
    s_reading.present = s_present;
    s_reading.initialized = s_present && s_reading.init_error == INIT_OK;
    s_reading.measurement_active = s_measurement_active;
    s_reading.next_measurement_ms = s_next_measurement_ms;

    if (s_measurement_active) {
        s_reading.active_elapsed_ms = now - s_measurement_started_ms;
        if (MEASUREMENT_ACTIVE_MS > 0 && s_reading.active_elapsed_ms < MEASUREMENT_ACTIVE_MS) {
            s_reading.active_remaining_ms = MEASUREMENT_ACTIVE_MS - s_reading.active_elapsed_ms;
        } else {
            s_reading.active_remaining_ms = 0;
        }
    } else {
        s_reading.active_elapsed_ms = 0;
        s_reading.active_remaining_ms = 0;
    }
}

static bool set_led_current(uint8_t red_current, uint8_t ir_current)
{
    bool ok = i2c_write_reg8(MAX30102_ADDR, 0x0C, red_current);
    ok = i2c_write_reg8(MAX30102_ADDR, 0x0D, ir_current) && ok;

    if (ok) {
        s_red_led_current = red_current;
        s_ir_led_current = ir_current;
        s_reading.red_led_current = red_current;
        s_reading.ir_led_current = ir_current;
    }

    return ok;
}

static void balance_led_currents(unsigned long now)
{
    if (now - s_last_led_balance_ms < REF_RED_LED_ADJUSTMENT_MS) {
        return;
    }

    uint8_t next_red = s_red_led_current;
    float diff = s_health.ir_dc - s_health.red_dc;
    s_reading.red_ir_dc_diff = static_cast<int32_t>(s_health.red_dc - s_health.ir_dc);

    if (diff > REF_ACCEPTABLE_INTENSITY_DIFF && next_red < LED_CURRENT_MAX) {
        ++next_red;
    } else if (-diff > REF_ACCEPTABLE_INTENSITY_DIFF && next_red > LED_CURRENT_MIN) {
        --next_red;
    }

    if (next_red != s_red_led_current) {
        set_led_current(next_red, s_ir_led_current);
    }

    s_last_led_balance_ms = now;
}

static void handle_saturation(uint32_t red, uint32_t ir, unsigned long now)
{
    if (red < RAW_SATURATION_LEVEL && ir < RAW_SATURATION_LEVEL) {
        return;
    }

    s_reading.saturated = true;
    if (now - s_last_saturation_adjust_ms < LED_SATURATION_ADJUSTMENT_MS) {
        return;
    }

    uint8_t next_red = s_red_led_current;
    uint8_t next_ir = s_ir_led_current;

    if (red >= RAW_SATURATION_LEVEL && next_red > LED_CURRENT_MIN) {
        next_red = next_red > LED_SATURATION_STEP ? next_red - LED_SATURATION_STEP : LED_CURRENT_MIN;
    }

    if (ir >= RAW_SATURATION_LEVEL && next_ir > LED_CURRENT_MIN) {
        next_ir = next_ir > LED_SATURATION_STEP ? next_ir - LED_SATURATION_STEP : LED_CURRENT_MIN;
    }

    if (next_red != s_red_led_current || next_ir != s_ir_led_current) {
        set_led_current(next_red, next_ir);
        reset_signal_state(true);
    }

    s_last_saturation_adjust_ms = now;
}

static uint8_t fifo_available_samples(uint8_t wr_ptr, uint8_t rd_ptr, uint8_t ovf)
{
    if (wr_ptr == 0xFF || rd_ptr == 0xFF || ovf == 0xFF) {
        return 0;
    }

    if (wr_ptr == rd_ptr) {
        return ovf > 0 ? 32 : 0;
    }

    return (wr_ptr - rd_ptr) & 0x1F;
}

static void reset_bpm_average()
{
    for (uint8_t i = 0; i < REF_BPM_SAMPLE_SIZE; ++i) {
        s_health.beat_intervals[i] = 0;
    }
    s_health.bpm_count = 0;
    s_health.bpm_index = 0;
}

static float median_interval_ms()
{
    if (s_health.bpm_count == 0) {
        return 0.0f;
    }

    uint16_t sorted[REF_BPM_SAMPLE_SIZE] = {0};
    for (uint8_t i = 0; i < s_health.bpm_count; ++i) {
        sorted[i] = s_health.beat_intervals[i];
    }

    for (uint8_t i = 1; i < s_health.bpm_count; ++i) {
        uint16_t key = sorted[i];
        int8_t j = i - 1;
        while (j >= 0 && sorted[j] > key) {
            sorted[j + 1] = sorted[j];
            --j;
        }
        sorted[j + 1] = key;
    }

    if ((s_health.bpm_count & 1) != 0) {
        return static_cast<float>(sorted[s_health.bpm_count / 2]);
    }

    uint8_t upper = s_health.bpm_count / 2;
    return (static_cast<float>(sorted[upper - 1]) + static_cast<float>(sorted[upper])) * 0.5f;
}

static bool is_duplicate_peak_interval(unsigned long beat_duration)
{
    if (beat_duration < REF_DUPLICATE_PEAK_MS) {
        return true;
    }

    if (s_health.bpm_count < 4) {
        return false;
    }

    float median_ms = median_interval_ms();
    if (median_ms <= 0.0f) {
        return false;
    }

    return static_cast<float>(beat_duration) < median_ms * REF_SHORT_INTERVAL_RATIO;
}

static void add_accepted_interval(uint16_t beat_duration)
{
    s_health.beat_intervals[s_health.bpm_index] = beat_duration;
    s_health.bpm_index = (s_health.bpm_index + 1) % REF_BPM_SAMPLE_SIZE;

    if (s_health.bpm_count < REF_BPM_SAMPLE_SIZE) {
        ++s_health.bpm_count;
    }
}

static void reset_signal_state(bool keep_values)
{
    s_health.ir_dc = 0.0f;
    s_health.red_dc = 0.0f;
    s_health.dc_ir = {};
    s_health.dc_red = {};
    s_health.lpb_ir = {};
    s_health.ir_ac_sq_sum = 0.0f;
    s_health.red_ac_sq_sum = 0.0f;
    s_health.samples_recorded = 0;
    s_health.pulses_detected = 0;
    s_health.pulse_state = REF_PULSE_IDLE;
    s_health.prev_sensor_value = 0.0f;
    s_health.values_went_down = 0;
    s_health.current_beat = 0;
    s_health.last_beat = 0;
    s_health.last_beat_threshold = 0.0f;

    for (uint8_t i = 0; i < REF_MEAN_FILTER_SIZE; ++i) {
        s_health.mean_values[i] = 0.0f;
    }
    s_health.mean_index = 0;
    s_health.mean_sum = 0.0f;
    s_health.mean_count = 0;

    reset_bpm_average();

    s_reading.ir_cardiogram = 0.0f;
    s_reading.dc_filtered_ir = 0.0f;
    s_reading.dc_filtered_red = 0.0f;
    s_reading.red_ir_dc_diff = 0;

    if (!keep_values) {
        s_health.bpm = 0.0f;
        s_health.spo2 = 0.0f;
        s_health.last_bpm_ms = 0;
        s_health.last_spo2_ms = 0;
        s_reading.bpm = 0.0f;
        s_reading.spo2 = 0.0f;
    }
}

static void expire_stale_values(unsigned long now)
{
    if (s_health.finger_present &&
        s_health.last_bpm_ms != 0 &&
        now - s_health.last_bpm_ms > BPM_HOLD_MS) {
        s_health.bpm = 0.0f;
        s_reading.bpm = 0.0f;
        s_health.last_bpm_ms = 0;
    }

    if (s_health.finger_present &&
        s_health.last_spo2_ms != 0 &&
        now - s_health.last_spo2_ms > SPO2_HOLD_MS) {
        s_health.spo2 = 0.0f;
        s_reading.spo2 = 0.0f;
        s_health.last_spo2_ms = 0;
    }
}

static bool read_fifo_sample(uint32_t &red, uint32_t &ir)
{
    uint8_t buf[6] = {0};
    if (!i2c_read_bytes(MAX30102_ADDR, 0x07, buf, sizeof(buf))) {
        return false;
    }

    red = ((uint32_t)(buf[0] & 0x03) << 16) |
          ((uint32_t)buf[1] << 8) |
          buf[2];

    ir = ((uint32_t)(buf[3] & 0x03) << 16) |
         ((uint32_t)buf[4] << 8) |
         buf[5];

    ++s_reading.sample_count;
    return true;
}

static RefDcFilter ref_dc_removal(float x, float prev_w)
{
    RefDcFilter filtered;
    filtered.w = x + REF_DC_ALPHA * prev_w;
    filtered.result = filtered.w - prev_w;
    return filtered;
}

static float ref_mean_diff(float value)
{
    s_health.mean_sum -= s_health.mean_values[s_health.mean_index];
    s_health.mean_values[s_health.mean_index] = value;
    s_health.mean_sum += s_health.mean_values[s_health.mean_index];
    s_health.mean_index = (s_health.mean_index + 1) % REF_MEAN_FILTER_SIZE;

    if (s_health.mean_count < REF_MEAN_FILTER_SIZE) {
        ++s_health.mean_count;
    }

    float avg = s_health.mean_sum / s_health.mean_count;
    return (avg - value) * 8.0f;
}

static void ref_lpb_filter(float value)
{
    s_health.lpb_ir.v[0] = s_health.lpb_ir.v[1];
    s_health.lpb_ir.v[1] =
        (2.452372752527856026e-1f * value) +
        (0.50952544949442879485f * s_health.lpb_ir.v[0]);
    s_health.lpb_ir.result = s_health.lpb_ir.v[0] + s_health.lpb_ir.v[1];
}

static bool ref_detect_pulse(float sensor_value, unsigned long now)
{
    if (sensor_value > REF_PULSE_MAX_THRESHOLD) {
        s_health.pulse_state = REF_PULSE_IDLE;
        s_health.prev_sensor_value = 0.0f;
        s_health.last_beat = 0;
        s_health.current_beat = 0;
        s_health.values_went_down = 0;
        s_health.last_beat_threshold = 0.0f;
        return false;
    }

    switch (s_health.pulse_state) {
    case REF_PULSE_IDLE:
        if (sensor_value >= REF_PULSE_MIN_THRESHOLD) {
            s_health.pulse_state = REF_PULSE_TRACE_UP;
            s_health.values_went_down = 0;
        }
        break;

    case REF_PULSE_TRACE_UP:
        if (sensor_value > s_health.prev_sensor_value) {
            s_health.current_beat = now;
            s_health.last_beat_threshold = sensor_value;
        } else {
            unsigned long beat_duration = s_health.current_beat - s_health.last_beat;
            s_health.last_beat = s_health.current_beat;

            float raw_bpm = 0.0f;
            if (beat_duration != 0) {
                raw_bpm = 60000.0f / static_cast<float>(beat_duration);
            }

            s_health.pulse_state = REF_PULSE_TRACE_DOWN;
            s_reading.last_candidate_ms = beat_duration <= UINT16_MAX ? static_cast<uint16_t>(beat_duration) : UINT16_MAX;
            s_reading.last_candidate_bpm = raw_bpm;
            s_reading.last_peak_value = s_health.last_beat_threshold;

            if (beat_duration > 2500) {
                reset_bpm_average();
            }

            if (raw_bpm < REF_VALID_BPM_MIN || is_duplicate_peak_interval(beat_duration)) {
                ++s_reading.rejected_pulses;
                return false;
            }

            uint16_t accepted_ms = beat_duration <= UINT16_MAX ?
                                   static_cast<uint16_t>(beat_duration) :
                                   UINT16_MAX;
            add_accepted_interval(accepted_ms);

            float robust_interval = median_interval_ms();
            float robust_bpm = robust_interval > 0.0f ? 60000.0f / robust_interval : raw_bpm;
            if (s_health.bpm <= 0.0f) {
                s_health.bpm = robust_bpm;
            } else {
                s_health.bpm = s_health.bpm * 0.65f + robust_bpm * 0.35f;
            }

            s_reading.bpm = s_health.bpm;
            s_reading.last_accepted_ms = s_reading.last_candidate_ms;
            s_reading.last_accepted_bpm = raw_bpm;
            ++s_reading.accepted_pulses;
            s_health.last_bpm_ms = now;
            return true;
        }
        break;

    case REF_PULSE_TRACE_DOWN:
        if (sensor_value < s_health.prev_sensor_value) {
            ++s_health.values_went_down;
        }

        if (sensor_value < REF_PULSE_MIN_THRESHOLD) {
            s_health.pulse_state = REF_PULSE_IDLE;
        }
        break;
    }

    s_health.prev_sensor_value = sensor_value;
    return false;
}

static void update_spo2_after_pulse(unsigned long now)
{
    if (s_health.samples_recorded == 0) {
        return;
    }

    float red_rms = sqrtf(s_health.red_ac_sq_sum / static_cast<float>(s_health.samples_recorded));
    float ir_rms = sqrtf(s_health.ir_ac_sq_sum / static_cast<float>(s_health.samples_recorded));

    if (red_rms <= 1.0f || ir_rms <= 1.0f) {
        return;
    }

    float ratio_rms = logf(red_rms) / logf(ir_rms);
    if (!isfinite(ratio_rms)) {
        return;
    }

    s_health.spo2 = 104.0f - 6.0f * ratio_rms;
    if (s_health.spo2 > 99.9f) {
        s_health.spo2 = 99.9f;
    } else if (s_health.spo2 < 0.0f) {
        s_health.spo2 = 0.0f;
    }

    s_reading.spo2 = s_health.spo2;
    s_health.last_spo2_ms = now;
}

static bool process_sample(uint32_t red, uint32_t ir, unsigned long now)
{
    s_reading.red = red;
    s_reading.ir = ir;
    s_reading.pulse_detected = false;
    s_reading.saturated = red >= RAW_SATURATION_LEVEL || ir >= RAW_SATURATION_LEVEL;

    if (ir < FINGER_OFF_IR) {
        if (s_health.low_ir_samples < FINGER_LOST_SAMPLES) {
            ++s_health.low_ir_samples;
        }

        if (s_health.low_ir_samples < FINGER_LOST_SAMPLES) {
            return false;
        }

        s_health.finger_present = false;
    } else {
        s_health.low_ir_samples = 0;
        if (ir >= FINGER_ON_IR) {
            s_health.finger_present = true;
        }
    }

    s_reading.finger_present = s_health.finger_present;

    if (!s_health.finger_present) {
        s_health.last_finger_present = false;
        reset_signal_state(false);
        return true;
    }

    if (!s_health.last_finger_present) {
        reset_signal_state(false);
        s_health.low_ir_samples = 0;
        s_health.last_finger_present = true;
    }

    handle_saturation(red, ir, now);
    if (s_reading.saturated) {
        return true;
    }

    float ref_ir = static_cast<float>(ir >> RAW_TO_REF_SHIFT);
    float ref_red = static_cast<float>(red >> RAW_TO_REF_SHIFT);
    s_health.dc_ir = ref_dc_removal(ref_ir, s_health.dc_ir.w);
    s_health.dc_red = ref_dc_removal(ref_red, s_health.dc_red.w);

    float mean_diff_ir = ref_mean_diff(s_health.dc_ir.result);
    ref_lpb_filter(mean_diff_ir);

    s_health.ir_dc = s_health.dc_ir.w;
    s_health.red_dc = s_health.dc_red.w;
    s_reading.red_ir_dc_diff = static_cast<int32_t>(s_health.red_dc - s_health.ir_dc);

    s_health.ir_ac_sq_sum += s_health.dc_ir.result * s_health.dc_ir.result;
    s_health.red_ac_sq_sum += s_health.dc_red.result * s_health.dc_red.result;
    if (s_health.samples_recorded < UINT16_MAX) {
        ++s_health.samples_recorded;
    }

    if (ref_detect_pulse(s_health.lpb_ir.result, now) && s_health.samples_recorded > 0) {
        s_reading.pulse_detected = true;
        ++s_health.pulses_detected;
        update_spo2_after_pulse(now);

        if ((s_health.pulses_detected % REF_RESET_SPO2_EVERY_N_PULSES) == 0) {
            s_health.ir_ac_sq_sum = 0.0f;
            s_health.red_ac_sq_sum = 0.0f;
            s_health.samples_recorded = 0;
        }
    }

    s_reading.ir_cardiogram = s_health.lpb_ir.result;
    s_reading.dc_filtered_ir = s_health.dc_ir.result;
    s_reading.dc_filtered_red = s_health.dc_red.result;

    balance_led_currents(now);
    return true;
}

static bool clear_fifo()
{
    bool ok = i2c_write_reg8(MAX30102_ADDR, 0x04, 0x00);
    ok = i2c_write_reg8(MAX30102_ADDR, 0x05, 0x00) && ok;
    ok = i2c_write_reg8(MAX30102_ADDR, 0x06, 0x00) && ok;
    return ok;
}

static bool set_shutdown(bool shutdown)
{
    uint8_t mode = i2c_read_reg8(MAX30102_ADDR, 0x09);
    if (mode == 0xFF) {
        return false;
    }

    uint8_t next = shutdown ? (mode | MODE_SHDN) : (mode & static_cast<uint8_t>(~MODE_SHDN));
    if (!shutdown && (next & 0x07) == 0) {
        next |= MODE_SPO2;
    }

    return i2c_write_reg8(MAX30102_ADDR, 0x09, next);
}

static bool start_measurement_window(unsigned long now, bool keep_values)
{
    if (!set_shutdown(false)) {
        return false;
    }

    clear_fifo();
    reset_signal_state(keep_values);
    s_measurement_active = true;
    s_measurement_started_ms = now;
    s_next_measurement_ms = MEASUREMENT_PERIOD_MS > 0 ? now + MEASUREMENT_PERIOD_MS : 0;
    s_last_rate_ms = now;
    s_last_rate_count = s_reading.sample_count;
    s_last_led_balance_ms = now;
    s_last_saturation_adjust_ms = now;
    refresh_debug_regs();
    refresh_schedule_fields(now);
    return true;
}

static void stop_measurement_window(unsigned long now)
{
    set_shutdown(true);
    s_measurement_active = false;
    s_reading.samples_last_second = 0;
    s_next_measurement_ms = s_measurement_started_ms + MEASUREMENT_PERIOD_MS;
    if (static_cast<long>(now - s_next_measurement_ms) >= 0) {
        s_next_measurement_ms = now + MEASUREMENT_PERIOD_MS;
    }
    refresh_debug_regs();
    refresh_schedule_fields(now);
}

static bool update_measurement_schedule(unsigned long now)
{
    if (s_measurement_active) {
        if (MEASUREMENT_ACTIVE_MS > 0 && now - s_measurement_started_ms >= MEASUREMENT_ACTIVE_MS) {
            stop_measurement_window(now);
        }
        refresh_schedule_fields(now);
        return s_measurement_active;
    }

    if (static_cast<long>(now - s_next_measurement_ms) >= 0) {
        start_measurement_window(now, true);
    }

    refresh_schedule_fields(now);
    return s_measurement_active;
}

bool max30102_begin()
{
    s_reading = {};
    s_health = {};
    s_present = i2c_device_exists(MAX30102_ADDR);
    s_reading.present = s_present;

    if (!s_present) {
        s_reading.init_error = INIT_NOT_FOUND;
        return false;
    }

    s_reading.rev_id = i2c_read_reg8(MAX30102_ADDR, 0xFE);
    s_reading.part_id = i2c_read_reg8(MAX30102_ADDR, 0xFF);

    if (s_reading.part_id != 0x15) {
        s_reading.init_error = INIT_BAD_PART_ID;
        refresh_debug_regs();
        s_present = false;
        s_reading.present = false;
        return false;
    }

    if (!write_reg_checked(0x09, MODE_RESET)) {
        s_reading.init_error = INIT_RESET_WRITE_FAILED;
        refresh_debug_regs();
        s_present = false;
        s_reading.present = false;
        return false;
    }

    unsigned long reset_start = millis();
    while ((i2c_read_reg8(MAX30102_ADDR, 0x09) & MODE_RESET) != 0 &&
           millis() - reset_start < 500) {
        delay(10);
    }

    if ((i2c_read_reg8(MAX30102_ADDR, 0x09) & MODE_RESET) != 0) {
        s_reading.init_error = INIT_RESET_TIMEOUT;
        refresh_debug_regs();
        s_present = false;
        s_reading.present = false;
        return false;
    }

    bool ok = true;
    ok = clear_fifo() && ok;
    ok = write_reg_checked(0x08, FIFO_CONFIG) && ok;
    ok = write_reg_checked(0x0A, SPO2_CONFIG) && ok;
    ok = write_reg_checked(0x0C, RED_LED_CURRENT) && ok;
    ok = write_reg_checked(0x0D, IR_LED_CURRENT) && ok;
    ok = write_reg_checked(0x09, MODE_SPO2) && ok;

    refresh_debug_regs();

    if (!ok) {
        s_reading.init_error = INIT_CONFIG_WRITE_FAILED;
        s_present = false;
        s_reading.present = false;
        return false;
    }

    if ((s_reading.mode_reg & 0x07) != MODE_SPO2) {
        s_reading.init_error = INIT_MODE_VERIFY_FAILED;
        s_present = false;
        s_reading.present = false;
        return false;
    }

    reset_signal_state(false);
    s_red_led_current = RED_LED_CURRENT;
    s_ir_led_current = IR_LED_CURRENT;
    if (!start_measurement_window(millis(), false)) {
        s_reading.init_error = INIT_START_FAILED;
        s_present = false;
        s_reading.present = false;
        s_reading.initialized = false;
        return false;
    }

    s_reading.init_error = INIT_OK;
    s_reading.initialized = true;
    return true;
}

bool max30102_update()
{
    if (!s_present) {
        return false;
    }

    unsigned long now = millis();
    if (!update_measurement_schedule(now)) {
        if (now - s_last_rate_ms >= 1000) {
            s_reading.samples_last_second = 0;
            s_last_rate_count = s_reading.sample_count;
            s_last_rate_ms = now;
            refresh_debug_regs();
        }
        return false;
    }

    bool got_sample = false;
    uint32_t red = 0;
    uint32_t ir = 0;
    uint8_t wr_ptr = i2c_read_reg8(MAX30102_ADDR, 0x04);
    uint8_t ovf = i2c_read_reg8(MAX30102_ADDR, 0x05);
    uint8_t rd_ptr = i2c_read_reg8(MAX30102_ADDR, 0x06);
    uint8_t available = fifo_available_samples(wr_ptr, rd_ptr, ovf);

    s_reading.fifo_wr_ptr = wr_ptr;
    s_reading.fifo_ovf = ovf;
    s_reading.fifo_rd_ptr = rd_ptr;

    for (uint8_t i = 0; i < available; ++i) {
        if (!read_fifo_sample(red, ir)) {
            break;
        }
        got_sample = process_sample(red, ir, millis()) || got_sample;
    }

    if (ovf > 0 && ovf != 0xFF) {
        i2c_write_reg8(MAX30102_ADDR, 0x05, 0x00);
    }

    now = millis();
    if (now - s_last_rate_ms >= 1000) {
        s_reading.samples_last_second =
            static_cast<uint16_t>(s_reading.sample_count - s_last_rate_count);
        s_last_rate_count = s_reading.sample_count;
        s_last_rate_ms = now;
        expire_stale_values(now);
        refresh_debug_regs();
    }

    refresh_schedule_fields(now);
    return got_sample;
}

const Max30102Reading &max30102_reading()
{
    return s_reading;
}

bool max30102_present()
{
    return s_present;
}

bool max30102_set_led_currents(uint8_t red_current, uint8_t ir_current)
{
    if (!s_present) {
        return false;
    }

    red_current = constrain(red_current, LED_CURRENT_MIN, LED_CURRENT_MAX);
    ir_current = constrain(ir_current, LED_CURRENT_MIN, LED_CURRENT_MAX);
    s_last_led_balance_ms = millis();
    return set_led_current(red_current, ir_current);
}

const char *max30102_init_error_text(uint8_t error)
{
    switch (error) {
    case INIT_OK:
        return "OK";
    case INIT_NOT_FOUND:
        return "not found";
    case INIT_BAD_PART_ID:
        return "bad PART_ID";
    case INIT_RESET_WRITE_FAILED:
        return "reset write failed";
    case INIT_RESET_TIMEOUT:
        return "reset timeout";
    case INIT_CONFIG_WRITE_FAILED:
        return "config write failed";
    case INIT_MODE_VERIFY_FAILED:
        return "mode verify failed";
    case INIT_START_FAILED:
        return "start failed";
    default:
        return "unknown";
    }
}
