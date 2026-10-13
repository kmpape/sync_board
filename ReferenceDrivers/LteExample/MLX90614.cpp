#include "MLX90614.h"
#include "Teensy_HID.h"

MLX90614::MLX90614(uint8_t addr) {
    i2caddr = addr;
}
//default initialization if needed
    // teensy_device.sendError("Bad key provided for MLX90614");



//To do - I dont know what to put into this initialise function yet
void MLX90614::Initialise(){
    // NOTE if you eventually want to initialise something here it will be a mess in case you ask it to do so before the i2c multiplexer connects to that one of the four thermometers
    // I.e. ;if you wever want to initialise something quickly at start of exeriment, be careful, may need to have to rewrite stuff.
}

uint16_t MLX90614::read16(uint8_t reg){
    //variables to store the 16 bits in 
    uint16_t data = 0;
    // uint8_t pec = 0;
    uint8_t rawBytes[4];
   
    Wire1.beginTransmission(i2caddr); //begin transmission to the device
    Wire1.write(reg); //send the register we want to read from
    Wire1.endTransmission(false); //keeps bus active for reading from device
   
    // request 3 bytes from the device:
   // Low byte, high byte and PEC
    Wire1.requestFrom(i2caddr, (uint8_t)3);

    //Wait for data to be available"
    for(uint8_t i = 0; i < 3; i++){
        rawBytes[i] = Wire1.read();    
    }
     // rawBytes[0] = lowByte, rawBytes[1] = highByte, rawBytes[2] = PEC
    
    uint8_t lowByte = rawBytes[0];
    uint8_t highByte = rawBytes[1];
    // uint8_t pec = rawBytes[2]; currently unused pec

    //Combine low and high into 16bit data
    data = (uint16_t(highByte) << 8) | lowByte;
    teensy_device.sendDebug("MLX90614 Read Data: %X", data);

    return data;
}

//read data from the sensor 

// Calculate 8-bit CRC (PEC) for MLX90614 checks
// Polynomial: x^8 + x^2 + x + 1 = 0x07
// uint8_t calculateCrc(const uint8_t *data, uint8_t len){
//     uint8_t crc = 0;
//     for (uint8_t i = 0; i < len; i++) {
//         uint8_t inByte = data[i];
//         for (uint8_t b = 0; b < 8; b++) {
//             uint8_t mix = (crc ^ inByte) & 0x80;
//             crc <<= 1;
//             inByte <<= 1;
//             if (mix) crc ^= 0x07; 
//         }
//     }
//     return crc;
// }

//we can set the emissivity according to material, but deafault maybe fine too

double MLX90614::getEmissivity(){
    uint16_t data = read16(EMISS);
    double emissivity = data / 65535.0;
    return emissivity;
}

void MLX90614::writeEmissivity(double emissivity){
    // 1) Constrain the emissivity between 0.1 and 1.0 as recommended
    if (emissivity < 0.1) emissivity = 0.1;
    else if (emissivity > 1.0) emissivity = 1.0;

    // 2) Convert float to 16-bit
    // Emissivity = (raw / 65535), so raw = round(emissivity * 65535)
    uint16_t data = (uint16_t)(emissivity * 65535.0 + 0.5);
   
    // 3) Erase cell (write 0x0000)
    {
        Wire1.beginTransmission(i2caddr);
        // EEPROM write command = 0x20 | address
        Wire1.write(0x20 | EMISS);  // 0x20 for writing to the EEPROM and followed by EMISS reg
        Wire1.write(0x00);          // lowByte
        Wire1.write(0x00);          // highByte
        Wire1.endTransmission();

        delay(5); // let sensor begin EEPROM programming
        
        Wire1.beginTransmission(i2caddr);
        Wire1.write(0x20 | EMISS);  // 0x24 if EMISS = 0x04
        Wire1.write(data & 0xFF);      // low byte
        Wire1.write((data >> 8) & 0xFF); // high byte
        Wire1.endTransmission();

        delay(5);
    }
}

// Convert a 16-bit raw temperature from the sensor
// to Celsius using the formula:
//   Temp [°C] = (rawValue * 0.02) - 273.15
double MLX90614::rawToCelsius(uint16_t rawValue) {
    double tempData = (double)rawValue * 0.02 - 273.15;
    return tempData;
}

//Read temperature of the object
// double MLX90614::getObjectTempC() {
//     uint16_t rawData = read16(T_OBJ1);
//     return rawToCelsius(rawData);
// }

double MLX90614::getAmbientTempC() {
    uint16_t rawData = read16(T_AMB);
    return rawToCelsius(rawData);
}

double MLX90614::getObjectTempC(){
    //variables to store the 16 bits in 
    // uint16_t data = 0;
    // uint8_t pec = 0;
    // uint8_t rawBytes[4];
   
    Wire1.beginTransmission(i2caddr); //begin transmission to the device
    Wire1.write(0x07); //send the register we want to read from
    Wire1.endTransmission(false); //keeps bus active for reading from device
    
    // request 3 bytes from the device:
   // Low byte, high byte and PEC
    Wire1.requestFrom(i2caddr, (uint8_t)3);

    byte byte1 = Wire1.read(); // MSB
    byte byte2 = Wire1.read(); // LSB
    Wire1.read(); // PEC - THis is an error correction bit we arent using...
    // byte byte3 = Wire1.read(); // PEC - THis is an error correction bit we arent using...

    uint16_t raw_data = 0;

    raw_data = (uint16_t(byte2) << 8) | byte1;
    double output = raw_data* 0.02 - 273.15;

    return output;
}