#include "ble_api.h"

#include "../drivers/max30102.h"
#include "../drivers/max30205.h"
#include "time_api.h"

#include <NimBLEDevice.h>
#include <math.h>
#include <string.h>

static constexpr const char *BLE_DEVICE_NAME = "ESP32-Watch";
static constexpr const char *SERVICE_UUID = "4fafc201-1fb5-459e-8fcc-c5c9c331914b";
static constexpr const char *BPM_UUID = "4fafc201-1fb5-459e-8fcc-c5c9c3310001";
static constexpr const char *SPO2_UUID = "4fafc201-1fb5-459e-8fcc-c5c9c3310002";
static constexpr const char *TEMP_UUID = "4fafc201-1fb5-459e-8fcc-c5c9c3310003";
static constexpr const char *WAVE_UUID = "4fafc201-1fb5-459e-8fcc-c5c9c3310004";
static constexpr const char *STATUS_UUID = "4fafc201-1fb5-459e-8fcc-c5c9c3310005";
static constexpr const char *COMMAND_UUID = "4fafc201-1fb5-459e-8fcc-c5c9c3310006";
static constexpr const char *DEVICE_INFO_UUID = "4fafc201-1fb5-459e-8fcc-c5c9c3310007";

static constexpr unsigned long TELEMETRY_INTERVAL_MS = 1000;
static constexpr uint8_t WAVE_BATCH_POINTS = 10;
static constexpr uint8_t WAVE_RING_POINTS = 80;
static constexpr uint8_t COMMAND_MAX_LEN = 8;

static constexpr uint8_t COMMAND_NONE = 0x00;
static constexpr uint8_t COMMAND_SET_TIME = 0x01;
static constexpr uint8_t COMMAND_WAVEFORM_BATCH = 0x02;

static constexpr uint8_t RESULT_NONE = 0x00;
static constexpr uint8_t RESULT_OK = 0x01;
static constexpr uint8_t RESULT_INVALID_LENGTH = 0x02;
static constexpr uint8_t RESULT_UNKNOWN_COMMAND = 0x03;
static constexpr uint8_t RESULT_FAILED = 0x04;
static constexpr uint8_t RESULT_BUSY = 0x05;

static NimBLEServer *s_server = nullptr;
static NimBLECharacteristic *s_bpm_char = nullptr;
static NimBLECharacteristic *s_spo2_char = nullptr;
static NimBLECharacteristic *s_temp_char = nullptr;
static NimBLECharacteristic *s_wave_char = nullptr;
static NimBLECharacteristic *s_status_char = nullptr;
static NimBLECharacteristic *s_command_char = nullptr;
static NimBLECharacteristic *s_device_info_char = nullptr;

static bool s_running = false;
static bool s_connected = false;
static unsigned long s_last_telemetry_ms = 0;

static int16_t s_wave_ring[WAVE_RING_POINTS] = {0};
static uint8_t s_wave_head = 0;
static uint8_t s_wave_count = 0;

static volatile bool s_command_pending = false;
static uint8_t s_command_buf[COMMAND_MAX_LEN] = {0};
static volatile uint8_t s_command_len = 0;
static uint8_t s_command_seq = 0;
static uint8_t s_last_command = COMMAND_NONE;
static uint8_t s_last_result = RESULT_NONE;

static void put_u16(uint8_t *buf, size_t &idx, uint16_t value)
{
    buf[idx++] = static_cast<uint8_t>(value & 0xFF);
    buf[idx++] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

static void put_u32(uint8_t *buf, size_t &idx, uint32_t value)
{
    buf[idx++] = static_cast<uint8_t>(value & 0xFF);
    buf[idx++] = static_cast<uint8_t>((value >> 8) & 0xFF);
    buf[idx++] = static_cast<uint8_t>((value >> 16) & 0xFF);
    buf[idx++] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

static void put_float(uint8_t *buf, size_t &idx, float value)
{
    uint32_t raw = 0;
    memcpy(&raw, &value, sizeof(raw));
    put_u32(buf, idx, raw);
}

static uint32_t read_u32_le(const uint8_t *buf)
{
    return static_cast<uint32_t>(buf[0]) |
           (static_cast<uint32_t>(buf[1]) << 8) |
           (static_cast<uint32_t>(buf[2]) << 16) |
           (static_cast<uint32_t>(buf[3]) << 24);
}

static void set_char_value(NimBLECharacteristic *characteristic, const uint8_t *data, size_t len)
{
    if (characteristic == nullptr) {
        return;
    }
    characteristic->setValue(data, static_cast<uint16_t>(len));
}

static void set_and_notify(NimBLECharacteristic *characteristic, const uint8_t *data, size_t len, bool notify)
{
    set_char_value(characteristic, data, len);
    if (notify && s_connected) {
        characteristic->notify(data, len);
    }
}

static uint32_t status_flags(const Max30102Reading &bio, const Max30205Reading &temp)
{
    uint32_t flags = 0;
    flags |= bio.present ? (1UL << 0) : 0;
    flags |= bio.initialized ? (1UL << 1) : 0;
    flags |= bio.finger_present ? (1UL << 2) : 0;
    flags |= bio.pulse_detected ? (1UL << 3) : 0;
    flags |= bio.saturated ? (1UL << 4) : 0;
    flags |= bio.settling ? (1UL << 5) : 0;
    flags |= bio.measurement_active ? (1UL << 6) : 0;
    flags |= temp.present ? (1UL << 7) : 0;
    flags |= temp.initialized ? (1UL << 8) : 0;
    flags |= temp.valid ? (1UL << 9) : 0;
    flags |= temp.contact_detected ? (1UL << 10) : 0;
    flags |= temp.body_valid ? (1UL << 11) : 0;
    flags |= time_api_is_valid() ? (1UL << 12) : 0;
    flags |= s_connected ? (1UL << 13) : 0;
    return flags;
}

static void update_metric_values(bool notify)
{
    const Max30102Reading &bio = max30102_reading();
    const Max30205Reading &temp = max30205_reading();

    uint8_t bpm_buf[4];
    size_t idx = 0;
    put_float(bpm_buf, idx, bio.bpm);
    set_and_notify(s_bpm_char, bpm_buf, sizeof(bpm_buf), notify);

    uint8_t spo2_buf[4];
    idx = 0;
    put_float(spo2_buf, idx, bio.spo2);
    set_and_notify(s_spo2_char, spo2_buf, sizeof(spo2_buf), notify);

    uint8_t temp_buf[8];
    idx = 0;
    put_float(temp_buf, idx, temp.temperature_c);
    put_float(temp_buf, idx, temp.body_temperature_c);
    set_and_notify(s_temp_char, temp_buf, sizeof(temp_buf), notify);
}

static void update_status_value(bool notify)
{
    const Max30102Reading &bio = max30102_reading();
    const Max30205Reading &temp = max30205_reading();
    uint8_t status_buf[13];
    size_t idx = 0;

    put_u32(status_buf, idx, status_flags(bio, temp));
    put_u16(status_buf, idx, bio.samples_last_second);
    put_u32(status_buf, idx, bio.sample_count);
    status_buf[idx++] = s_command_seq;
    status_buf[idx++] = s_last_command;
    status_buf[idx++] = s_last_result;

    set_and_notify(s_status_char, status_buf, idx, notify);
}

static int16_t quantize_wave(float value)
{
    value = constrain(value, -32768.0f, 32767.0f);
    if (value >= 0.0f) {
        return static_cast<int16_t>(value + 0.5f);
    }
    return static_cast<int16_t>(value - 0.5f);
}

static void push_wave_sample(const Max30102Reading &reading)
{
    int16_t value = 0;
    if (reading.measurement_active && reading.finger_present && !reading.saturated) {
        value = quantize_wave(reading.ir_cardiogram);
    }

    s_wave_ring[s_wave_head] = value;
    s_wave_head = (s_wave_head + 1) % WAVE_RING_POINTS;
    if (s_wave_count < WAVE_RING_POINTS) {
        ++s_wave_count;
    }
}

static void update_waveform_value(bool notify)
{
    uint8_t wave_buf[WAVE_BATCH_POINTS * 2] = {0};
    const uint8_t count = s_wave_count < WAVE_BATCH_POINTS ? s_wave_count : WAVE_BATCH_POINTS;
    const uint8_t leading_zeroes = WAVE_BATCH_POINTS - count;
    uint8_t idx = leading_zeroes * 2;
    uint8_t ring_idx = (s_wave_head + WAVE_RING_POINTS - count) % WAVE_RING_POINTS;

    for (uint8_t i = 0; i < count; ++i) {
        int16_t value = s_wave_ring[ring_idx];
        wave_buf[idx++] = static_cast<uint8_t>(value & 0xFF);
        wave_buf[idx++] = static_cast<uint8_t>((static_cast<uint16_t>(value) >> 8) & 0xFF);
        ring_idx = (ring_idx + 1) % WAVE_RING_POINTS;
    }

    set_and_notify(s_wave_char, wave_buf, sizeof(wave_buf), notify);
}

static void set_command_result(uint8_t command, uint8_t result)
{
    ++s_command_seq;
    s_last_command = command;
    s_last_result = result;
}

static void handle_command(const uint8_t *data, uint8_t len)
{
    if (len == 0) {
        return;
    }

    const uint8_t command = data[0];
    switch (command) {
    case COMMAND_SET_TIME:
        if (len != 5) {
            set_command_result(command, RESULT_INVALID_LENGTH);
        } else if (time_api_set_epoch(static_cast<long>(read_u32_le(data + 1)))) {
            set_command_result(command, RESULT_OK);
        } else {
            set_command_result(command, RESULT_FAILED);
        }
        update_status_value(true);
        break;

    case COMMAND_WAVEFORM_BATCH:
        if (len != 1) {
            set_command_result(command, RESULT_INVALID_LENGTH);
            update_status_value(true);
        } else {
            set_command_result(command, RESULT_OK);
            update_waveform_value(true);
            update_status_value(true);
        }
        break;

    default:
        set_command_result(command, RESULT_UNKNOWN_COMMAND);
        update_status_value(true);
        break;
    }
}

static void consume_pending_command()
{
    if (!s_command_pending) {
        return;
    }

    uint8_t len = s_command_len;
    uint8_t data[COMMAND_MAX_LEN] = {0};
    if (len > COMMAND_MAX_LEN) {
        len = COMMAND_MAX_LEN;
    }
    memcpy(data, s_command_buf, len);
    s_command_pending = false;
    handle_command(data, len);
}

class WatchServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer *server, NimBLEConnInfo &connInfo) override
    {
        (void)server;
        (void)connInfo;
        s_connected = true;
        update_status_value(true);
    }

    void onDisconnect(NimBLEServer *server, NimBLEConnInfo &connInfo, int reason) override
    {
        (void)server;
        (void)connInfo;
        (void)reason;
        s_connected = false;
        if (s_running) {
            NimBLEDevice::startAdvertising();
        }
    }
};

class CommandCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *characteristic, NimBLEConnInfo &connInfo) override
    {
        (void)connInfo;
        if (s_command_pending) {
            set_command_result(COMMAND_NONE, RESULT_BUSY);
            update_status_value(true);
            return;
        }

        NimBLEAttValue value = characteristic->getValue();
        const size_t raw_len = value.length();
        uint8_t len = raw_len > COMMAND_MAX_LEN ? COMMAND_MAX_LEN : static_cast<uint8_t>(raw_len);

        memcpy(s_command_buf, value.data(), len);
        s_command_len = len;
        s_command_pending = true;
    }
};

static WatchServerCallbacks s_server_callbacks;
static CommandCallbacks s_command_callbacks;

void ble_api_start()
{
    if (s_running) {
        return;
    }

    s_connected = false;
    s_command_pending = false;
    s_command_len = 0;
    s_last_telemetry_ms = 0;

    NimBLEDevice::init(BLE_DEVICE_NAME);
    s_server = NimBLEDevice::createServer();
    s_server->setCallbacks(&s_server_callbacks, false);

    NimBLEService *service = s_server->createService(SERVICE_UUID);
    s_bpm_char = service->createCharacteristic(BPM_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY, 4);
    s_spo2_char = service->createCharacteristic(SPO2_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY, 4);
    s_temp_char = service->createCharacteristic(TEMP_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY, 8);
    s_wave_char = service->createCharacteristic(WAVE_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY, 20);
    s_status_char = service->createCharacteristic(STATUS_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY, 13);
    s_command_char = service->createCharacteristic(COMMAND_UUID, NIMBLE_PROPERTY::WRITE, COMMAND_MAX_LEN);
    s_device_info_char = service->createCharacteristic(DEVICE_INFO_UUID, NIMBLE_PROPERTY::READ, 48);
    s_command_char->setCallbacks(&s_command_callbacks);

    const char device_info[] = "WatchTest;proto=1;board=esp32-c3";
    set_char_value(s_device_info_char,
                   reinterpret_cast<const uint8_t *>(device_info),
                   strlen(device_info));
    update_metric_values(false);
    update_waveform_value(false);
    update_status_value(false);

    service->start();

    NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
    advertising->addServiceUUID(SERVICE_UUID);
    advertising->setName(BLE_DEVICE_NAME);
    advertising->enableScanResponse(true);
    advertising->start();

    s_running = true;
}

void ble_api_stop()
{
    if (!s_running) {
        return;
    }

    s_running = false;
    s_connected = false;
    s_command_pending = false;
    NimBLEDevice::stopAdvertising();
    NimBLEDevice::deinit(true);

    s_server = nullptr;
    s_bpm_char = nullptr;
    s_spo2_char = nullptr;
    s_temp_char = nullptr;
    s_wave_char = nullptr;
    s_status_char = nullptr;
    s_command_char = nullptr;
    s_device_info_char = nullptr;
}

void ble_api_update(bool got_sample)
{
    if (!s_running) {
        return;
    }

    if (got_sample) {
        push_wave_sample(max30102_reading());
    }

    consume_pending_command();

    const unsigned long now = millis();
    if (now - s_last_telemetry_ms >= TELEMETRY_INTERVAL_MS) {
        s_last_telemetry_ms = now;
        update_metric_values(true);
        update_status_value(true);
    }
}

bool ble_api_is_running()
{
    return s_running;
}

bool ble_api_is_connected()
{
    return s_running && s_connected;
}

const char *ble_api_status_text()
{
    if (!s_running) {
        return "BLE off";
    }
    return s_connected ? "phone connected" : "advertising";
}
