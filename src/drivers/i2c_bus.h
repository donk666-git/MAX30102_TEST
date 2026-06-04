#ifndef I2C_BUS_H
#define I2C_BUS_H

#include <Arduino.h>
#include <Wire.h>

void i2c_bus_init();
bool i2c_device_exists(uint8_t addr);
void i2c_scan(Stream &out);
bool i2c_write_reg8(uint8_t addr, uint8_t reg, uint8_t data);
uint8_t i2c_read_reg8(uint8_t addr, uint8_t reg);
bool i2c_read_bytes(uint8_t addr, uint8_t reg, uint8_t *buf, uint8_t len);

#endif
