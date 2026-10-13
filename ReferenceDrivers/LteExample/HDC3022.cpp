#include "HDC3022.h"
// #include "HDC302x.h"
#include "Teensy_HID.h"

HDC3022::HDC3022(){
    teensy_device.sendError("Bad key provided for HDC3022");
    i2caddr = 0;
}

HDC3022::HDC3022(uint8_t addr) {
    i2caddr = addr;
}

void HDC3022::Initialise(){
    //Initialise to continuous reading low noise every 2 seconds

    Wire1.beginTransmission(i2caddr);
    Wire1.write(SOFT_RESET_MSB);
    Wire1.write(SOFT_RESET_LSB);
    uint8_t result = Wire1.endTransmission();
    if (result != 0) {
        teensy_device.sendError("HDC3022: Failed to reset device, Error code: %d", result);
    }

    delay(20); // wait 20ms after reset

    Wire1.beginTransmission(i2caddr);
    Wire1.write(AUTO2SEC_LOW_NOISE_MODE_MSB);
    Wire1.write(AUTO2SEC_LOW_NOISE_MODE_LSB);
    uint8_t result2 = Wire1.endTransmission();
    if (result2 != 0) {
        teensy_device.sendError("HDC3022: Failed to set readout mode, Error code: %d", result2);
    }
}



HDC3022::HDC3022Data HDC3022::readContinuous(){

    HDC3022Data result;
    Wire1.beginTransmission(i2caddr);
    Wire1.write(MEASUREMENT_READOUT_MSB); 
    Wire1.write(MEASUREMENT_READOUT_LSB);
    Wire1.endTransmission(false); // send a restart after transmission

    Wire1.requestFrom(i2caddr, uint8_t(6)); // request 6 bytes of data then send a stop condition
    uint8_t rawTempMSB = Wire1.read();
    uint8_t rawTempLSB = Wire1.read();
    uint8_t tempCRC = Wire1.read();

    uint8_t rawHumidityMSB = Wire1.read();
    uint8_t rawHumidityLSB = Wire1.read();
    uint8_t humidityCRC = Wire1.read();

    uint16_t rawTemp = ((rawTempMSB << 8) | rawTempLSB);
    uint16_t rawHumidity = ((rawHumidityMSB << 8) | rawHumidityLSB);

    if ((crc8_nrsc5_gen(rawTemp) != tempCRC) || (crc8_nrsc5_gen(rawHumidity) != humidityCRC)) {
        teensy_device.sendInfo("HDC3022 CRC Fail - using last good value");
        // if CRC fails its usually because we have tried to read humidity sensor too quickly, in this case we return the last good value instead of bogus, so the programme can keep running as normal
        result.temperature = last_temperature;
        result.humidity = last_humidity;

    } else {
        result.temperature = -45.0 + (175 * (float(rawTemp) / 65535.0));
        result.humidity = (float(rawHumidity) / 65535.0) * 100.0;
        // this relies on the humidity sensor getting at least one good reading before failure
        last_temperature = result.temperature;
        last_humidity = result.humidity;
    }

    return result;

}

HDC3022::HDC3022Data HDC3022::readSingleShot(){

    HDC3022Data result;
    Wire1.beginTransmission(i2caddr);
    Wire1.write(SINGLE_LOW_NOISE_MODE_MSB);
    Wire1.write(SINGLE_LOW_NOISE_MODE_LSB);
    Wire1.endTransmission(false); // send a restart after transmission
    delay(12); // delay 12 milliseconds for conversion time

    Wire1.requestFrom(i2caddr, uint8_t(6)); // request 6 bytes of data then send a stop condition
    uint8_t rawTempMSB = Wire1.read();
    uint8_t rawTempLSB = Wire1.read();
    uint8_t tempCRC = Wire1.read();

    uint8_t rawHumidityMSB = Wire1.read();
    uint8_t rawHumidityLSB = Wire1.read();
    uint8_t humidityCRC = Wire1.read();

    uint16_t rawTemp = ((rawTempMSB << 8) | rawTempLSB);
    if (crc8_nrsc5_gen(rawTemp) != tempCRC) {
        teensy_device.sendInfo("HDC3022 temperature CRC error - set to -99.0");
        result.temperature = -99.0; // Set to invalid value
    } else {
        result.temperature = -45.0 + (175 * (float(rawTemp) / 65535.0));
    }
    
    uint16_t rawHumidity = ((rawHumidityMSB << 8) | rawHumidityLSB);
    if (crc8_nrsc5_gen(rawHumidity) != humidityCRC) {
        teensy_device.sendInfo("HDC3022 humidity CRC error - set to -99.0");
        result.humidity = -99.0; // Set to invalid value
    } else {
        result.humidity = (float(rawHumidity) / 65535.0) * 100.0;  // First output wrong, rest look correct
    }

    // result.temperature = -45.0 + (175 * (float(rawTemp) / 65535.0));
    // result.humidity = (float(rawHumidity) / 65535.0) * 100.0;

    delay(12);

    return result;      
}

uint8_t HDC3022::crc8_nrsc5_gen(uint16_t input){
    static constexpr uint8_t crc_table[256] = { // https://crccalc.com/?crc=02&method=CRC-8/NRSC-5&datatype=hex&outtype=hex
        0x00, 0x31, 0x62, 0x53, 0xC4, 0xF5, 0xA6, 0x97, 0xB9, 0x88, 0xDB, 0xEA, 0x7D, 0x4C, 0x1F, 0x2E,
        0x43, 0x72, 0x21, 0x10, 0x87, 0xB6, 0xE5, 0xD4, 0xFA, 0xCB, 0x98, 0xA9, 0x3E, 0x0F, 0x5C, 0x6D,
        0x86, 0xB7, 0xE4, 0xD5, 0x42, 0x73, 0x20, 0x11, 0x3F, 0x0E, 0x5D, 0x6C, 0xFB, 0xCA, 0x99, 0xA8,
        0xC5, 0xF4, 0xA7, 0x96, 0x01, 0x30, 0x63, 0x52, 0x7C, 0x4D, 0x1E, 0x2F, 0xB8, 0x89, 0xDA, 0xEB,
        0x3D, 0x0C, 0x5F, 0x6E, 0xF9, 0xC8, 0x9B, 0xAA, 0x84, 0xB5, 0xE6, 0xD7, 0x40, 0x71, 0x22, 0x13,
        0x7E, 0x4F, 0x1C, 0x2D, 0xBA, 0x8B, 0xD8, 0xE9, 0xC7, 0xF6, 0xA5, 0x94, 0x03, 0x32, 0x61, 0x50,
        0xBB, 0x8A, 0xD9, 0xE8, 0x7F, 0x4E, 0x1D, 0x2C, 0x02, 0x33, 0x60, 0x51, 0xC6, 0xF7, 0xA4, 0x95,
        0xF8, 0xC9, 0x9A, 0xAB, 0x3C, 0x0D, 0x5E, 0x6F, 0x41, 0x70, 0x23, 0x12, 0x85, 0xB4, 0xE7, 0xD6,
        0x7A, 0x4B, 0x18, 0x29, 0xBE, 0x8F, 0xDC, 0xED, 0xC3, 0xF2, 0xA1, 0x90, 0x07, 0x36, 0x65, 0x54,
        0x39, 0x08, 0x5B, 0x6A, 0xFD, 0xCC, 0x9F, 0xAE, 0x80, 0xB1, 0xE2, 0xD3, 0x44, 0x75, 0x26, 0x17,
        0xFC, 0xCD, 0x9E, 0xAF, 0x38, 0x09, 0x5A, 0x6B, 0x45, 0x74, 0x27, 0x16, 0x81, 0xB0, 0xE3, 0xD2,
        0xBF, 0x8E, 0xDD, 0xEC, 0x7B, 0x4A, 0x19, 0x28, 0x06, 0x37, 0x64, 0x55, 0xC2, 0xF3, 0xA0, 0x91,
        0x47, 0x76, 0x25, 0x14, 0x83, 0xB2, 0xE1, 0xD0, 0xFE, 0xCF, 0x9C, 0xAD, 0x3A, 0x0B, 0x58, 0x69,
        0x04, 0x35, 0x66, 0x57, 0xC0, 0xF1, 0xA2, 0x93, 0xBD, 0x8C, 0xDF, 0xEE, 0x79, 0x48, 0x1B, 0x2A,
        0xC1, 0xF0, 0xA3, 0x92, 0x05, 0x34, 0x67, 0x56, 0x78, 0x49, 0x1A, 0x2B, 0xBC, 0x8D, 0xDE, 0xEF,
        0x82, 0xB3, 0xE0, 0xD1, 0x46, 0x77, 0x24, 0x15, 0x3B, 0x0A, 0x59, 0x68, 0xFF, 0xCE, 0x9D, 0xAC
    };

    uint8_t crc = 0xFF; // CRC initialiser

    // Process high byte of input
    uint8_t highByte = (input >> 8) & 0xFF; // retrieve highbyte
    crc ^= highByte;                        // xor with initialiser
    crc = crc_table[crc];                   // lookup in table
    
    // Process low byte of input
    uint8_t lowByte = input & 0xFF;
    crc ^= lowByte;
    crc = crc_table[crc];
    
    return crc;
}