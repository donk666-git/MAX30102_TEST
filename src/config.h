#ifndef WATCH_TEST_CONFIG_H
#define WATCH_TEST_CONFIG_H

#include <stdint.h>

static constexpr int PIN_UART_RX = 20;
static constexpr int PIN_UART_TX = 21;

static constexpr int PIN_I2C_SDA = 3;
static constexpr int PIN_I2C_SCL = 2;

static constexpr int PIN_TFT_CS = 4;
static constexpr int PIN_TFT_DC = 10;
static constexpr int PIN_TFT_RST = 7;
static constexpr int PIN_TFT_MOSI = 6;
static constexpr int PIN_TFT_SCLK = 5;

static constexpr uint8_t MAX30102_ADDR = 0x57;

#endif
