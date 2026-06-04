#include "i2c_bus.h"
#include "../config.h"

void i2c_bus_init()
{
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setClock(100000);
}

bool i2c_device_exists(uint8_t addr)
{
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

void i2c_scan(Stream &out)
{
    out.println();
    out.println("I2C scan:");
    int count = 0;

    for (uint8_t addr = 1; addr < 127; ++addr) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            out.print("  found 0x");
            if (addr < 16) {
                out.print('0');
            }
            out.println(addr, HEX);
            ++count;
        }
    }

    out.print("  devices: ");
    out.println(count);
}

bool i2c_write_reg8(uint8_t addr, uint8_t reg, uint8_t data)
{
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(data);
    return Wire.endTransmission() == 0;
}

uint8_t i2c_read_reg8(uint8_t addr, uint8_t reg)
{
    Wire.beginTransmission(addr);
    Wire.write(reg);

    if (Wire.endTransmission(false) != 0) {
        return 0xFF;
    }

    if (Wire.requestFrom(addr, static_cast<uint8_t>(1)) != 1) {
        return 0xFF;
    }

    return Wire.read();
}

bool i2c_read_bytes(uint8_t addr, uint8_t reg, uint8_t *buf, uint8_t len)
{
    Wire.beginTransmission(addr);
    Wire.write(reg);

    if (Wire.endTransmission(false) != 0) {
        return false;
    }

    if (Wire.requestFrom(addr, len) != len) {
        return false;
    }

    for (uint8_t i = 0; i < len; ++i) {
        buf[i] = Wire.read();
    }

    return true;
}
