#include <Arduino.h>

#include "config.h"
#include "drivers/i2c_bus.h"
#include "drivers/max30102.h"
#include "drivers/max30205.h"
#include "comms/ble_api.h"
#include "comms/time_api.h"
#include "comms/web_api.h"
#include "ui/max30102_debug_ui.h"

#include <time.h>

static constexpr unsigned long DISPLAY_REFRESH_MS = 200;
static constexpr unsigned long SERIAL_REPORT_MS = 2000;
static constexpr unsigned long WEB_UPDATE_MS = 20;
static constexpr size_t SERIAL_LINE_MAX = 96;

static bool s_serial_verbose = false;

static void report_serial()
{
    const Max30102Reading &r = max30102_reading();
    const Max30205Reading &t = max30205_reading();
    char time_buf[8];
    char date_buf[8];
    time_api_get_strings(time_buf, sizeof(time_buf), date_buf, sizeof(date_buf));

    Serial.print("Present=");
    Serial.print(r.present ? 1 : 0);

    Serial.print(" Init=");
    Serial.print(r.initialized ? 1 : 0);

    Serial.print(" Err=");
    Serial.print(r.init_error);
    Serial.print("(");
    Serial.print(max30102_init_error_text(r.init_error));
    Serial.print(")");

    Serial.print(" Rev=0x");
    Serial.print(r.rev_id, HEX);

    Serial.print(" Part=0x");
    Serial.print(r.part_id, HEX);

    Serial.print(" ");

    Serial.print("Active=");
    Serial.print(r.measurement_active ? 1 : 0);

    Serial.print(" Rem=");
    if (r.measurement_active) {
        if (r.active_remaining_ms > 0) {
            Serial.print(r.active_remaining_ms / 1000UL);
            Serial.print("s");
        } else {
            Serial.print("LIVE");
        }
    } else {
        long wait_ms = static_cast<long>(r.next_measurement_ms - millis());
        Serial.print((wait_ms > 0 ? wait_ms : 0) / 1000L);
        Serial.print("s");
    }

    Serial.print(" Samples=");
    Serial.print(r.sample_count);

    Serial.print(" Rate=");
    Serial.print(r.samples_last_second);
    Serial.print("/s");

    Serial.print(" IR=");
    Serial.print(r.ir);

    Serial.print(" AC=");
    Serial.print(r.ir_cardiogram, 1);

    Serial.print(" BPM=");
    if (r.bpm > 0.0f) {
        Serial.print(r.bpm, 1);
    } else {
        Serial.print("--");
    }

    Serial.print(" SpO2=");
    if (r.spo2 > 0.0f) {
        Serial.print(r.spo2, 1);
        Serial.print("%");
    } else {
        Serial.print("--");
    }

    Serial.print(" R=");
    if (r.spo2_ratio > 0.0f) {
        Serial.print(r.spo2_ratio, 3);
    } else {
        Serial.print("--");
    }

    Serial.print(" Amp=");
    Serial.print(r.spo2_red_amp, 0);
    Serial.print("/");
    Serial.print(r.spo2_ir_amp, 0);

    Serial.print(" Temp=");
    if (t.valid) {
        Serial.print(t.temperature_c, 2);
        Serial.print("C");
    } else {
        Serial.print("--");
    }

    Serial.print(" Body=");
    if (t.body_valid) {
        Serial.print(t.body_temperature_c, 2);
        Serial.print("C");
    } else {
        Serial.print("--");
    }

    Serial.print(" TBase=");
    if (t.baseline_temperature_c > 0.0f) {
        Serial.print(t.baseline_temperature_c, 2);
    } else {
        Serial.print("--");
    }

    Serial.print(" TContact=");
    Serial.print(t.contact_detected ? 1 : 0);
    Serial.print("/");
    Serial.print(t.contact_elapsed_ms / 1000UL);
    Serial.print("s/");
    Serial.print(t.stability_range_c, 2);

    Serial.print(" TStat=");
    Serial.print(t.present ? 1 : 0);
    Serial.print("/");
    Serial.print(t.initialized ? 1 : 0);
    Serial.print("/");
    Serial.print(t.error_count);
    Serial.print("/");
    Serial.print(t.init_error);
    Serial.print("(");
    Serial.print(max30205_init_error_text(t.init_error));
    Serial.print(")");

    Serial.print(" Time=");
    Serial.print(time_buf);
    Serial.print(" ");
    Serial.print(date_buf);
    Serial.print("/");
    Serial.print(time_api_is_valid() ? 1 : 0);

    Serial.print(" WiFi=");
    Serial.print(web_api_status_text());
    Serial.print(" AP=");
    Serial.print(web_api_ap_ip());
    Serial.print(" Clients=");
    Serial.print(web_api_client_count());

    Serial.print(" BLE=");
    Serial.print(ble_api_status_text());

    Serial.print(" Finger=");
    Serial.print(r.finger_present ? 1 : 0);

    Serial.print(" Pulse=");
    Serial.print(r.pulse_detected ? 1 : 0);

    Serial.print(" Settle=");
    if (r.settling) {
        Serial.print(r.settle_remaining_ms);
        Serial.print("ms");
    } else {
        Serial.print("0");
    }

    Serial.print(" Cand=");
    Serial.print(r.last_candidate_ms);
    Serial.print("ms/");
    if (r.last_candidate_bpm > 0.0f) {
        Serial.print(r.last_candidate_bpm, 1);
    } else {
        Serial.print("--");
    }

    Serial.print(" Beat=");
    Serial.print(r.last_accepted_ms);
    Serial.print("ms/");
    if (r.last_accepted_bpm > 0.0f) {
        Serial.print(r.last_accepted_bpm, 1);
    } else {
        Serial.print("--");
    }

    Serial.print(" Peak=");
    Serial.print(r.last_peak_value, 0);

    Serial.print(" Acc/Rej=");
    Serial.print(r.accepted_pulses);
    Serial.print("/");
    Serial.print(r.rejected_pulses);

    Serial.print(" Sat=");
    Serial.print(r.saturated ? 1 : 0);

    Serial.print(" FIFO=");
    Serial.print(r.fifo_wr_ptr, HEX);
    Serial.print("/");
    Serial.print(r.fifo_rd_ptr, HEX);
    Serial.print("/");
    Serial.print(r.fifo_ovf, HEX);

    Serial.print(" LED=");
    Serial.print(r.red_led_current, HEX);
    Serial.print("/");
    Serial.print(r.ir_led_current, HEX);

    Serial.print(" Mode=0x");
    Serial.print(r.mode_reg, HEX);

    Serial.print(" SpO2Cfg=0x");
    Serial.println(r.spo2_config, HEX);
}

static char *trim_line(char *line)
{
    while (*line == ' ' || *line == '\t') {
        ++line;
    }

    char *end = line + strlen(line);
    while (end > line && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) {
        --end;
    }
    *end = '\0';
    return line;
}

static bool set_manual_time(int year, int month, int day, int hour, int minute, int second)
{
    struct tm tm_set = {};
    tm_set.tm_year = year - 1900;
    tm_set.tm_mon = month - 1;
    tm_set.tm_mday = day;
    tm_set.tm_hour = hour;
    tm_set.tm_min = minute;
    tm_set.tm_sec = second;
    tm_set.tm_isdst = -1;

    time_t epoch = mktime(&tm_set);
    if (epoch < 0) {
        return false;
    }

    return time_api_set_epoch(static_cast<long>(epoch));
}

static void handle_serial_command(char *raw_line)
{
    char *line = trim_line(raw_line);
    if (line[0] == '\0') {
        return;
    }

    if (strcmp(line, "LOUD") == 0) {
        s_serial_verbose = true;
        Serial.println("serial output ON");
    } else if (strcmp(line, "QUIET") == 0) {
        s_serial_verbose = false;
        Serial.println("serial output OFF");
    } else if (strcmp(line, "WEB ON") == 0) {
        ble_api_stop();
        web_api_init();
        Serial.print("web ON AP=");
        Serial.print(WEB_AP_SSID);
        Serial.print(" URL=http://");
        Serial.print(web_api_ap_ip());
        Serial.println("/");
    } else if (strcmp(line, "WEB OFF") == 0) {
        web_api_stop();
        Serial.println("web OFF");
    } else if (strcmp(line, "BLE ON") == 0) {
        web_api_stop();
        ble_api_start();
        Serial.println("BLE ON name=ESP32-Watch");
    } else if (strcmp(line, "BLE OFF") == 0) {
        ble_api_stop();
        Serial.println("BLE OFF");
    } else if (strcmp(line, "STATUS") == 0) {
        report_serial();
    } else if (strncmp(line, "SET ", 4) == 0) {
        int y = 0;
        int mo = 0;
        int d = 0;
        int h = 0;
        int mi = 0;
        int s = 0;
        if (sscanf(line, "SET %d %d %d %d %d %d", &y, &mo, &d, &h, &mi, &s) == 6 &&
            set_manual_time(y, mo, d, h, mi, s)) {
            Serial.printf("RTC set: %04d-%02d-%02d %02d:%02d:%02d\n", y, mo, d, h, mi, s);
        } else {
            Serial.println("Usage: SET 2026 6 3 14 30 0");
        }
    } else {
        Serial.println("Commands: LOUD, QUIET, STATUS, WEB ON, WEB OFF, BLE ON, BLE OFF, SET y m d h m s");
    }
}

static void handle_serial_input()
{
    static char line[SERIAL_LINE_MAX];
    static size_t len = 0;

    while (Serial.available() > 0) {
        char c = static_cast<char>(Serial.read());

        if (c == '\n' || c == '\r') {
            if (len > 0) {
                line[len] = '\0';
                handle_serial_command(line);
                len = 0;
            }
            continue;
        }

        if (len < SERIAL_LINE_MAX - 1) {
            line[len++] = c;
        } else {
            len = 0;
            Serial.println("serial command too long");
        }
    }
}

void setup()
{
    delay(1000);

    Serial.begin(115200, SERIAL_8N1, PIN_UART_RX, PIN_UART_TX);
    Serial.setTimeout(10);
    delay(500);

    Serial.println();
    Serial.println("===== MAX30102 reference algorithm test =====");

    time_api_init();
    max30102_debug_ui_begin();
    i2c_bus_init();
    delay(300);
    i2c_scan(Serial);

    bool max_found = max30102_begin();
    Serial.print("MAX30102 ");
    Serial.println(max_found ? "init OK" : "init FAILED");
    Serial.print("Init detail: ");
    Serial.println(max30102_init_error_text(max30102_reading().init_error));
    Serial.print("REV_ID=0x");
    Serial.print(max30102_reading().rev_id, HEX);
    Serial.print(" PART_ID=0x");
    Serial.println(max30102_reading().part_id, HEX);
    Serial.println("Mode: continuous realtime sampling");

    bool temp_found = max30205_begin();
    Serial.print("MAX30205 ");
    Serial.println(temp_found ? "init OK" : "init FAILED");
    Serial.print("Temp init detail: ");
    Serial.println(max30205_init_error_text(max30205_reading().init_error));
    Serial.print("Temp config=0x");
    Serial.println(max30205_reading().config, HEX);

    Serial.println("Web AP and BLE default OFF. Type WEB ON or BLE ON.");
    Serial.println("Commands: LOUD, QUIET, STATUS, WEB ON, WEB OFF, BLE ON, BLE OFF, SET y m d h m s");

    max30102_debug_ui_update(max30102_reading(), max30205_reading());
}

void loop()
{
    static unsigned long last_report = 0;
    static unsigned long last_display_refresh = 0;
    static unsigned long last_web_update = 0;

    handle_serial_input();
    bool got_sample = max30102_update();
    max30205_update();
    ble_api_update(got_sample);
    const Max30102Reading &reading = max30102_reading();
    const Max30205Reading &temperature = max30205_reading();
    max30102_debug_ui_push_sample(reading, got_sample);

    unsigned long now = millis();
    if (now - last_web_update >= WEB_UPDATE_MS) {
        last_web_update = now;
        web_api_update();
    }

    if (s_serial_verbose && now - last_report >= SERIAL_REPORT_MS) {
        last_report = now;
        report_serial();
    }

    if (now - last_display_refresh >= DISPLAY_REFRESH_MS) {
        last_display_refresh = now;
        max30102_debug_ui_update(reading, temperature);
    }

    delay(1);
}
