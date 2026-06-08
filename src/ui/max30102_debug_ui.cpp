#include "max30102_debug_ui.h"

#include <Adafruit_GC9A01A.h>
#include <SPI.h>
#include <math.h>
#include "../config.h"
#include "../comms/time_api.h"
#include "../comms/web_api.h"

static constexpr uint16_t COLOR_BG = 0x0801;
static constexpr uint16_t COLOR_PANEL = 0x1082;
static constexpr uint16_t COLOR_SURFACE = 0x10A2;
static constexpr uint16_t COLOR_GRID = 0x2104;
static constexpr uint16_t COLOR_MUTED = 0x7BEF;
static constexpr uint16_t COLOR_TEXT = 0xE73C;
static constexpr uint16_t COLOR_RED = 0xF98C;
static constexpr uint16_t COLOR_BLUE = 0x07FF;
static constexpr uint16_t COLOR_GREEN = 0x07E0;
static constexpr uint16_t COLOR_GOLD = 0xFEA0;
static constexpr uint16_t COLOR_ORANGE = 0xFD20;
static constexpr uint16_t COLOR_FAIL = 0xF800;

static constexpr int SCREEN_CX = 120;
static constexpr int SCREEN_CY = 120;
static constexpr int WAVE_X = 18;
static constexpr int WAVE_Y = 144;
static constexpr int WAVE_W = 204;
static constexpr int WAVE_H = 58;
static constexpr uint8_t WAVE_POINTS = 96;
static constexpr float WAVE_MIN_DISPLAY_RANGE = 700.0f;
static constexpr float WAVE_OUTLIER_LIMIT = 1800.0f;

static Adafruit_GC9A01A tft(PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);

static bool s_display_ready = false;
static float s_wave_samples[WAVE_POINTS] = {0.0f};
static uint8_t s_wave_index = 0;
static bool s_wave_primed = false;
static float s_wave_filtered = 0.0f;
static bool s_wave_filter_ready = false;

static void draw_centered_text(const char *text, int16_t y, uint8_t size, uint16_t color, uint16_t bg)
{
    tft.setFont();
    tft.setTextSize(size);
    tft.setTextColor(color, bg);

    int16_t x1 = 0;
    int16_t y1 = 0;
    uint16_t w = 0;
    uint16_t h = 0;
    tft.getTextBounds(text, 0, y, &x1, &y1, &w, &h);
    tft.setCursor(SCREEN_CX - static_cast<int>(w) / 2, y);
    tft.print(text);
}

static void draw_small_text(const char *text, int16_t x, int16_t y, uint16_t color, uint16_t bg)
{
    tft.setFont();
    tft.setTextSize(1);
    tft.setTextColor(color, bg);
    tft.setCursor(x, y);
    tft.print(text);
}

static void draw_right_text(const char *text, int16_t right_x, int16_t y, uint8_t size, uint16_t color, uint16_t bg)
{
    tft.setFont();
    tft.setTextSize(size);
    tft.setTextColor(color, bg);

    int16_t x1 = 0;
    int16_t y1 = 0;
    uint16_t w = 0;
    uint16_t h = 0;
    tft.getTextBounds(text, 0, y, &x1, &y1, &w, &h);
    tft.setCursor(right_x - static_cast<int>(w), y);
    tft.print(text);
}

static void draw_status_dot(int16_t x, int16_t y, uint16_t color)
{
    tft.fillCircle(x, y, 3, color);
}

static void draw_metric_column(const char *label,
                               const char *value,
                               int16_t x,
                               int16_t w,
                               uint16_t color)
{
    tft.fillRect(x, 112, w, 24, COLOR_BG);
    draw_small_text(label, x + 2, 112, COLOR_MUTED, COLOR_BG);

    tft.setFont();
    tft.setTextSize(1);
    tft.setTextColor(color, COLOR_BG);
    int16_t x1 = 0;
    int16_t y1 = 0;
    uint16_t tw = 0;
    uint16_t th = 0;
    tft.getTextBounds(value, 0, 124, &x1, &y1, &tw, &th);
    tft.setCursor(x + (w - static_cast<int>(tw)) / 2, 124);
    tft.print(value);
}

static void push_wave_value(float value, bool active)
{
    if (!active) {
        s_wave_filtered = 0.0f;
        s_wave_filter_ready = false;
        value = 0.0f;
    } else if (!s_wave_filter_ready) {
        s_wave_filtered = value;
        s_wave_filter_ready = true;
    } else {
        float delta = value - s_wave_filtered;
        delta = constrain(delta, -WAVE_OUTLIER_LIMIT, WAVE_OUTLIER_LIMIT);
        s_wave_filtered += delta * 0.35f;
        value = s_wave_filtered;
    }

    s_wave_samples[s_wave_index] = value;
    s_wave_index = (s_wave_index + 1) % WAVE_POINTS;
    if (s_wave_index == 0) {
        s_wave_primed = true;
    }
}

static const char *display_status_text(const Max30102Reading &reading, uint16_t &color)
{
    if (!reading.present) {
        color = COLOR_FAIL;
        return "MAX30102 NOT FOUND";
    }

    if (!reading.initialized) {
        color = COLOR_FAIL;
        return "INIT FAILED";
    }

    if (!reading.measurement_active) {
        color = COLOR_MUTED;
        return "WAIT NEXT WINDOW";
    }

    if (!reading.finger_present) {
        color = COLOR_ORANGE;
        return "PLACE FINGER";
    }

    if (reading.settling) {
        color = COLOR_GOLD;
        return "SIGNAL SETTLING";
    }

    if (reading.saturated) {
        color = COLOR_FAIL;
        return "SIGNAL SATURATED";
    }

    if (reading.bpm <= 0.0f) {
        color = COLOR_GOLD;
        return "MEASURING";
    }

    color = COLOR_GREEN;
    return "LOCKED";
}

static void draw_wave_frame()
{
    tft.drawRoundRect(WAVE_X, WAVE_Y, WAVE_W, WAVE_H, 4, COLOR_PANEL);
    tft.drawLine(WAVE_X + 4, WAVE_Y + WAVE_H / 2, WAVE_X + WAVE_W - 5, WAVE_Y + WAVE_H / 2, COLOR_GRID);
    for (int x = WAVE_X + 4 + 34; x < WAVE_X + WAVE_W - 4; x += 34) {
        tft.drawLine(x, WAVE_Y + 6, x, WAVE_Y + WAVE_H - 7, COLOR_GRID);
    }
    draw_small_text("PPG", WAVE_X + 6, WAVE_Y + 6, COLOR_MUTED, COLOR_BG);
}

static void draw_waveform(const Max30102Reading &reading)
{
    tft.fillRect(WAVE_X + 2, WAVE_Y + 2, WAVE_W - 4, WAVE_H - 4, COLOR_BG);
    draw_wave_frame();

    uint8_t count = s_wave_primed ? WAVE_POINTS : s_wave_index;
    int mid_y = WAVE_Y + WAVE_H / 2;

    if (!reading.measurement_active || !reading.finger_present || reading.saturated || count < 2) {
        tft.drawLine(WAVE_X + 7, mid_y, WAVE_X + WAVE_W - 8, mid_y, COLOR_MUTED);
        return;
    }

    float mean_value = 0.0f;
    for (uint8_t i = 0; i < count; ++i) {
        uint8_t idx = s_wave_primed ? (s_wave_index + i) % WAVE_POINTS : i;
        mean_value += s_wave_samples[idx];
    }
    mean_value /= count;

    float abs_mean = 0.0f;
    for (uint8_t i = 0; i < count; ++i) {
        uint8_t idx = s_wave_primed ? (s_wave_index + i) % WAVE_POINTS : i;
        abs_mean += fabsf(s_wave_samples[idx] - mean_value);
    }
    abs_mean /= count;

    float span = fmaxf(WAVE_MIN_DISPLAY_RANGE, abs_mean * 6.0f);
    float min_value = mean_value - span * 0.5f;
    float max_value = mean_value + span * 0.5f;

    int last_x = 0;
    int last_y = 0;
    for (uint8_t i = 0; i < count; ++i) {
        uint8_t idx = s_wave_primed ? (s_wave_index + i) % WAVE_POINTS : i;
        float plotted = constrain(s_wave_samples[idx], min_value, max_value);
        int x = WAVE_X + 7 + (static_cast<int>(i) * (WAVE_W - 14)) / (count - 1);
        int y = WAVE_Y + WAVE_H - 8 -
                static_cast<int>((plotted - min_value) * static_cast<float>(WAVE_H - 16) / span);
        y = constrain(y, WAVE_Y + 7, WAVE_Y + WAVE_H - 8);

        if (i > 0) {
            tft.drawLine(last_x, last_y, x, y, COLOR_RED);
        }
        last_x = x;
        last_y = y;
    }
}

static void draw_display_shell()
{
    tft.fillScreen(COLOR_BG);
    tft.drawCircle(SCREEN_CX, SCREEN_CY, 118, COLOR_SURFACE);
    tft.drawCircle(SCREEN_CX, SCREEN_CY, 112, COLOR_GRID);
    tft.drawCircle(SCREEN_CX, SCREEN_CY, 103, COLOR_PANEL);

    tft.drawLine(34, 42, 206, 42, COLOR_GRID);
    tft.drawLine(36, 106, 204, 106, COLOR_GRID);
    tft.drawLine(82, 112, 82, 134, COLOR_GRID);
    tft.drawLine(158, 112, 158, 134, COLOR_GRID);
    draw_wave_frame();
}

void max30102_debug_ui_begin()
{
    SPI.begin(PIN_TFT_SCLK, -1, PIN_TFT_MOSI, PIN_TFT_CS);
    tft.begin();
    tft.setRotation(0);
    draw_display_shell();
    s_display_ready = true;
}

void max30102_debug_ui_push_sample(const Max30102Reading &reading, bool got_sample)
{
    if (!got_sample) {
        return;
    }

    push_wave_value(reading.ir_cardiogram,
                    reading.measurement_active && reading.finger_present && !reading.saturated);
}

void max30102_debug_ui_update(const Max30102Reading &reading, const Max30205Reading &temperature)
{
    if (!s_display_ready) {
        return;
    }

    char buf[28];
    uint16_t status_color = COLOR_MUTED;
    const char *status = display_status_text(reading, status_color);
    char time_buf[8];
    char date_buf[8];
    time_api_get_strings(time_buf, sizeof(time_buf), date_buf, sizeof(date_buf));

    tft.fillRect(24, 12, 192, 31, COLOR_BG);
    draw_small_text(date_buf, 40, 15, time_api_is_valid() ? COLOR_MUTED : COLOR_ORANGE, COLOR_BG);
    draw_centered_text(time_buf, 12, 2, time_api_is_valid() ? COLOR_TEXT : COLOR_MUTED, COLOR_BG);

    const uint8_t clients = web_api_client_count();
    const bool web_running = web_api_is_running();
    snprintf(buf, sizeof(buf), web_running ? "AP%u" : "WEB OFF", clients);
    draw_right_text(buf,
                    200,
                    15,
                    1,
                    clients > 0 ? COLOR_GREEN : (web_running ? COLOR_BLUE : COLOR_MUTED),
                    COLOR_BG);
    draw_status_dot(207, 19, clients > 0 ? COLOR_GREEN : (web_running ? COLOR_BLUE : COLOR_MUTED));

    tft.fillRect(34, 45, 172, 10, COLOR_BG);
    draw_centered_text(status, 46, 1, status_color, COLOR_BG);

    tft.fillRect(36, 58, 168, 47, COLOR_BG);
    if (reading.bpm > 0.0f) {
        snprintf(buf, sizeof(buf), "%d", static_cast<int>(reading.bpm + 0.5f));
        draw_centered_text(buf, 58, 4, COLOR_RED, COLOR_BG);
    } else {
        draw_centered_text("--", 58, 4, COLOR_MUTED, COLOR_BG);
    }
    draw_centered_text("BPM", 94, 1, COLOR_MUTED, COLOR_BG);

    char spo2_value[12];
    if (reading.spo2 > 0.0f) {
        snprintf(spo2_value, sizeof(spo2_value), "%.0f%%", reading.spo2);
    } else {
        snprintf(spo2_value, sizeof(spo2_value), "--");
    }

    char temp_value[12];
    if (temperature.body_valid) {
        snprintf(temp_value, sizeof(temp_value), "%.1fC", temperature.body_temperature_c);
    } else if (temperature.valid) {
        snprintf(temp_value, sizeof(temp_value), "%.1fC", temperature.temperature_c);
    } else {
        snprintf(temp_value, sizeof(temp_value), "--");
    }

    char hz_value[12];
    snprintf(hz_value, sizeof(hz_value), "%u", reading.samples_last_second);

    draw_metric_column("SPO2", spo2_value, 24, 54, reading.spo2 > 0.0f ? COLOR_BLUE : COLOR_MUTED);
    draw_metric_column(temperature.body_valid ? "BODY" : "TEMP",
                       temp_value,
                       91,
                       58,
                       temperature.body_valid ? COLOR_GREEN :
                       (temperature.valid ? COLOR_GOLD : COLOR_MUTED));
    draw_metric_column("HZ", hz_value, 172, 38, reading.samples_last_second > 0 ? COLOR_GREEN : COLOR_MUTED);

    draw_waveform(reading);

    tft.fillRect(38, 211, 164, 20, COLOR_BG);
    snprintf(buf,
             sizeof(buf),
             "BIO %s  TMP %s",
             reading.initialized ? "OK" : "--",
             temperature.valid ? "OK" : "--");
    draw_centered_text(buf, 213, 1, COLOR_MUTED, COLOR_BG);
}
