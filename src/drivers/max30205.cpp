#include "max30205.h"
#include "i2c_bus.h"
#include "../config.h"

static constexpr uint8_t REG_TEMPERATURE = 0x00;
static constexpr uint8_t REG_CONFIGURATION = 0x01;
static constexpr uint8_t CONFIG_CONTINUOUS = 0x00;
static constexpr float TEMP_LSB_C = 0.00390625f;
static constexpr unsigned long TEMP_UPDATE_MS = 1000;
static constexpr uint8_t RECOVER_AFTER_ERRORS = 3;
static constexpr float BODY_TEMP_OFFSET_C = 2.7f;
static constexpr float CONTACT_ON_DELTA_C = 1.2f;
static constexpr float CONTACT_OFF_DELTA_C = 0.6f;
static constexpr float BASELINE_ALPHA = 0.04f;
static constexpr uint8_t STABLE_WINDOW_SIZE = 8;
static constexpr unsigned long MIN_CONTACT_STABLE_MS = 8000;
static constexpr float STABLE_WINDOW_RANGE_C = 0.18f;

enum InitError : uint8_t {
    INIT_OK = 0,
    INIT_NOT_FOUND = 1,
    INIT_CONFIG_WRITE_FAILED = 2,
    INIT_READ_FAILED = 3
};

static bool s_present = false;
static Max30205Reading s_reading;
static unsigned long s_last_update_ms = 0;
static unsigned long s_contact_started_ms = 0;
static uint8_t s_consecutive_errors = 0;
static float s_stable_window[STABLE_WINDOW_SIZE] = {0.0f};
static uint8_t s_stable_index = 0;
static uint8_t s_stable_count = 0;

static void reset_stable_window()
{
    for (uint8_t i = 0; i < STABLE_WINDOW_SIZE; ++i) {
        s_stable_window[i] = 0.0f;
    }
    s_stable_index = 0;
    s_stable_count = 0;
    s_reading.stability_range_c = 0.0f;
}

static void push_stable_sample(float temperature)
{
    s_stable_window[s_stable_index] = temperature;
    s_stable_index = (s_stable_index + 1) % STABLE_WINDOW_SIZE;

    if (s_stable_count < STABLE_WINDOW_SIZE) {
        ++s_stable_count;
    }
}

static bool stable_window_average(float &average, float &range)
{
    if (s_stable_count < STABLE_WINDOW_SIZE) {
        return false;
    }

    float sum = 0.0f;
    float min_value = s_stable_window[0];
    float max_value = s_stable_window[0];

    for (uint8_t i = 0; i < STABLE_WINDOW_SIZE; ++i) {
        float value = s_stable_window[i];
        sum += value;
        if (value < min_value) {
            min_value = value;
        }
        if (value > max_value) {
            max_value = value;
        }
    }

    average = sum / static_cast<float>(STABLE_WINDOW_SIZE);
    range = max_value - min_value;
    return true;
}

static void update_body_temperature_state(float temperature, unsigned long now)
{
    if (s_reading.baseline_temperature_c <= 0.0f) {
        s_reading.baseline_temperature_c = temperature;
    }

    float delta = temperature - s_reading.baseline_temperature_c;

    if (!s_reading.contact_detected) {
        s_reading.baseline_temperature_c =
            s_reading.baseline_temperature_c * (1.0f - BASELINE_ALPHA) +
            temperature * BASELINE_ALPHA;
        s_reading.body_valid = false;
        s_reading.body_temperature_c = 0.0f;
        s_reading.stable_sensor_temperature_c = 0.0f;
        s_reading.contact_elapsed_ms = 0;

        if (delta >= CONTACT_ON_DELTA_C) {
            s_reading.contact_detected = true;
            s_contact_started_ms = now;
            reset_stable_window();
        }
        return;
    }

    s_reading.contact_elapsed_ms = now - s_contact_started_ms;
    push_stable_sample(temperature);

    if (delta < CONTACT_OFF_DELTA_C) {
        s_reading.contact_detected = false;
        s_reading.body_valid = false;
        s_reading.body_temperature_c = 0.0f;
        s_reading.stable_sensor_temperature_c = 0.0f;
        s_reading.contact_elapsed_ms = 0;
        reset_stable_window();
        return;
    }

    float average = 0.0f;
    float range = 0.0f;
    if (!stable_window_average(average, range)) {
        return;
    }

    s_reading.stability_range_c = range;
    if (s_reading.contact_elapsed_ms < MIN_CONTACT_STABLE_MS || range > STABLE_WINDOW_RANGE_C) {
        s_reading.body_valid = false;
        return;
    }

    s_reading.stable_sensor_temperature_c = average;
    s_reading.body_temperature_c = average + BODY_TEMP_OFFSET_C;
    s_reading.body_valid = true;
}

static bool read_temperature_raw(int16_t &raw)
{
    uint8_t buf[2] = {0};

    Wire.beginTransmission(MAX30205_ADDR);
    Wire.write(REG_TEMPERATURE);
    if (Wire.endTransmission(true) != 0) {
        return false;
    }

    delayMicroseconds(250);

    if (Wire.requestFrom(MAX30205_ADDR, static_cast<uint8_t>(sizeof(buf)), static_cast<uint8_t>(true)) != sizeof(buf)) {
        return false;
    }

    buf[0] = Wire.read();
    buf[1] = Wire.read();
    raw = static_cast<int16_t>((static_cast<uint16_t>(buf[0]) << 8) | buf[1]);
    return true;
}

static bool recover_sensor()
{
    if (!i2c_device_exists(MAX30205_ADDR)) {
        s_present = false;
        s_reading.present = false;
        s_reading.initialized = false;
        s_reading.valid = false;
        s_reading.init_error = INIT_NOT_FOUND;
        return false;
    }

    s_present = true;
    s_reading.present = true;

    if (!i2c_write_reg8(MAX30205_ADDR, REG_CONFIGURATION, CONFIG_CONTINUOUS)) {
        s_reading.initialized = false;
        s_reading.valid = false;
        s_reading.init_error = INIT_CONFIG_WRITE_FAILED;
        return false;
    }

    s_reading.config = i2c_read_reg8(MAX30205_ADDR, REG_CONFIGURATION);
    s_reading.initialized = true;
    s_reading.init_error = INIT_OK;
    s_consecutive_errors = 0;
    return true;
}

static bool read_temperature(unsigned long now)
{
    int16_t raw = 0;
    if (!read_temperature_raw(raw)) {
        s_reading.valid = false;
        ++s_reading.error_count;
        if (s_consecutive_errors < UINT8_MAX) {
            ++s_consecutive_errors;
        }
        return false;
    }

    float temperature = static_cast<float>(raw) * TEMP_LSB_C;
    if (temperature < -40.0f || temperature > 125.0f) {
        s_reading.valid = false;
        ++s_reading.error_count;
        if (s_consecutive_errors < UINT8_MAX) {
            ++s_consecutive_errors;
        }
        return false;
    }

    s_reading.raw = raw;
    s_reading.temperature_c = temperature;
    s_reading.valid = true;
    s_reading.last_update_ms = now;
    update_body_temperature_state(temperature, now);
    s_consecutive_errors = 0;
    ++s_reading.sample_count;
    return true;
}

bool max30205_begin()
{
    s_reading = {};
    reset_stable_window();
    s_contact_started_ms = 0;
    s_consecutive_errors = 0;

    unsigned long now = millis();
    s_last_update_ms = now;
    if (!recover_sensor()) {
        return false;
    }

    if (!read_temperature(now)) {
        s_reading.init_error = INIT_READ_FAILED;
        return false;
    }

    s_reading.init_error = INIT_OK;
    return true;
}

bool max30205_update()
{
    unsigned long now = millis();
    if (now - s_last_update_ms < TEMP_UPDATE_MS) {
        return false;
    }

    s_last_update_ms = now;
    if (!s_present || !s_reading.initialized) {
        if (s_consecutive_errors < UINT8_MAX) {
            ++s_consecutive_errors;
        }
        if (!recover_sensor()) {
            return false;
        }
    }

    bool ok = read_temperature(now);
    if (!ok && s_consecutive_errors >= RECOVER_AFTER_ERRORS && recover_sensor()) {
        ok = read_temperature(now);
    }

    return ok;
}

const Max30205Reading &max30205_reading()
{
    return s_reading;
}

bool max30205_present()
{
    return s_present;
}

const char *max30205_init_error_text(uint8_t error)
{
    switch (error) {
    case INIT_OK:
        return "OK";
    case INIT_NOT_FOUND:
        return "not found";
    case INIT_CONFIG_WRITE_FAILED:
        return "config write failed";
    case INIT_READ_FAILED:
        return "read failed";
    default:
        return "unknown";
    }
}
