#include <Arduino.h>

#include "config.h"
#include "drivers/i2c_bus.h"
#include "drivers/max30102.h"
#include "drivers/max30205.h"
#include "ui/max30102_debug_ui.h"

static constexpr unsigned long DISPLAY_REFRESH_MS = 200;
static constexpr unsigned long SERIAL_REPORT_MS = 1000;

static void report_serial()
{
    const Max30102Reading &r = max30102_reading();
    const Max30205Reading &t = max30205_reading();

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

void setup()
{
    delay(1000);

    Serial.begin(115200, SERIAL_8N1, PIN_UART_RX, PIN_UART_TX);
    delay(500);

    Serial.println();
    Serial.println("===== MAX30102 reference algorithm test =====");

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

    max30102_debug_ui_update(max30102_reading(), max30205_reading());
}

void loop()
{
    static unsigned long last_report = 0;
    static unsigned long last_display_refresh = 0;

    bool got_sample = max30102_update();
    max30205_update();
    const Max30102Reading &reading = max30102_reading();
    const Max30205Reading &temperature = max30205_reading();
    max30102_debug_ui_push_sample(reading, got_sample);

    unsigned long now = millis();
    if (now - last_report >= SERIAL_REPORT_MS) {
        last_report = now;
        report_serial();
    }

    if (now - last_display_refresh >= DISPLAY_REFRESH_MS) {
        last_display_refresh = now;
        max30102_debug_ui_update(reading, temperature);
    }

    delay(1);
}
