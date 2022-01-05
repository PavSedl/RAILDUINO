/*
 *   DS2438.cpp
 *
 *   by Joe Bechter
 *
 *   (C) 2012, bechter.com
 *
 *   All files, software, schematics and designs are provided as-is with no warranty.
 *   All files, software, schematics and designs are for experimental/hobby use.
 *   Under no circumstances should any part be used for critical systems where safety,
 *   life or property depends upon it. You are responsible for all use.
 *   You are free to use, modify, derive or otherwise extend for your own non-commercial purposes provided
 *       1. No part of this software or design may be used to cause injury or death to humans or animals.
 *       2. Use is non-commercial.
 *       3. Credit is given to the author (i.e. portions © bechter.com), and provide a link to the original source.
 *
 */

#include "DS2438.h"

DS2438::DS2438(OneWire *ow) {
    _ow = ow;
};

void DS2438::begin(uint8_t mode) {
    _mode = mode & (DS2438_MODE_CHA | DS2438_MODE_CHB | DS2438_MODE_TEMPERATURE);
    _temperature = 0;
    _voltageA = 0.0;
    _voltageB = 0.0;
    _error = true;
    _timestamp = 0;
}

void DS2438::update(uint8_t *address) {
    uint8_t data[9];
    _address = address;
    _error = true;
    _timestamp = millis();

    if (_mode & DS2438_MODE_CHA || _mode == DS2438_MODE_TEMPERATURE) {
        boolean doTemperature = _mode & DS2438_MODE_TEMPERATURE;
        if (!startConversion(DS2438_CHA, doTemperature,_address)) {
            return;
        }
        if (!readPageZero(data,_address))
            return;
        if (doTemperature) {
            _temperature = (double)(((((int16_t)data[2]) << 8) | (data[1] & 0x0ff)) >> 3) * 0.03125;
        }
        if (_mode & DS2438_MODE_CHA) {
            _voltageA = (((data[4] << 8) & 0x00300) | (data[3] & 0x0ff)) / 100.0;
        }
    }
    if (_mode & DS2438_MODE_CHB) {
        boolean doTemperature = _mode & DS2438_MODE_TEMPERATURE && !(_mode & DS2438_MODE_CHA);
        if (!startConversion(DS2438_CHB, doTemperature,_address)) {
            return;
        }
        if (!readPageZero(data,_address))
            return;
        if (doTemperature) {
            _temperature = (double)(((((int16_t)data[2]) << 8) | (data[1] & 0x0ff)) >> 3) * 0.03125;
        }
        _voltageB = abs(((data[6] << 8) | (data[5]))) / 4096.0;
        
    }
    _error = false;
}

double DS2438::getTemperature() {
    return _temperature;
}

float DS2438::getVoltage(int channel) {
    if (channel == DS2438_CHA) {
        return _voltageA;
    } else if (channel == DS2438_CHB) {
        return _voltageB;
    } else {
        return 0.0;
    }
}

boolean DS2438::isError() {
    return _error;
}

unsigned long DS2438::getTimestamp() {
    return _timestamp;
}

boolean DS2438::startConversion(int channel, boolean doTemperature, uint8_t *address) {
    _address = address;
    if (!selectChannel(channel,_address))
        return false;
    _ow->reset();
    _ow->select(_address);
    if (doTemperature) {
        _ow->write(DS2438_TEMPERATURE_CONVERSION_COMMAND, 0);
        delay(DS2438_TEMPERATURE_DELAY);
        _ow->reset();
        _ow->select(_address);
    }
    _ow->write(DS2438_VOLTAGE_CONVERSION_COMMAND, 0);
    delay(DS2438_VOLTAGE_CONVERSION_DELAY);
    return true;
}

boolean DS2438::selectChannel(int channel, uint8_t *address) {
    _address = address;
    uint8_t data[9];
    if (readPageZero(data,_address)) {
        if (channel == DS2438_CHB)
            data[0] = data[0] | 0x08;
        else
            data[0] = data[0] & 0xf7;
        writePageZero(data,_address);
        return true;
    }
    return false;
}

void DS2438::writePageZero(uint8_t *data, uint8_t *address) {
    _address = address;
    _ow->reset();
    _ow->select(_address);
    _ow->write(DS2438_WRITE_SCRATCHPAD_COMMAND, 0);
    _ow->write(DS2438_PAGE_0, 0);
    for (int i = 0; i < 8; i++)
        _ow->write(data[i], 0);
    _ow->reset();
    _ow->select(_address);
    _ow->write(DS2438_COPY_SCRATCHPAD_COMMAND, 0);
    _ow->write(DS2438_PAGE_0, 0);
}

boolean DS2438::readPageZero(uint8_t *data, uint8_t *address) {
    _address = address;
    _ow->reset();
    _ow->select(_address);
    _ow->write(DS2438_RECALL_MEMORY_COMMAND, 0);
    _ow->write(DS2438_PAGE_0, 0);
    _ow->reset();
    _ow->select(_address);
    _ow->write(DS2438_READ_SCRATCHPAD_COMMAND, 0);
    _ow->write(DS2438_PAGE_0, 0);
    for (int i = 0; i < 9; i++)
        data[i] = _ow->read();
    return _ow->crc8(data, 8) == data[8];
}


