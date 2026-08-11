#pragma once
#include <Wire.h>
#include <stdint.h>

// Driver minimo para MCP4725 usando "Fast Mode Write": 2 bytes por muestra,
// sin bit de comando ni escritura a EEPROM. Es lo mas rapido que el chip
// soporta por I2C, necesario para poder sostener un sample rate de audio.
class MCP4725Fast {
public:
    void begin(uint8_t address, TwoWire &wire = Wire) {
        _addr = address;
        _wire = &wire;
    }

    // value12: 0..4095
    inline void write12(uint16_t value12) {
        _wire->beginTransmission(_addr);
        _wire->write((uint8_t)((value12 >> 8) & 0x0F)); // C2 C1 PD1 PD0 D11..D8 (C/PD forzados a 0)
        _wire->write((uint8_t)(value12 & 0xFF));         // D7..D0
        _wire->endTransmission();
    }

private:
    uint8_t _addr = 0x60;
    TwoWire *_wire = &Wire;
};
