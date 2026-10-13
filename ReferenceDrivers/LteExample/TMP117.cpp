#include "TMP117.h"
#include "Teensy_HID.h"

// Default constructor
TMP117::TMP117() : i2caddr(0) {
    // Default initialization if needed
    teensy_device.sendError("Bad key provided for TMP117");
}

// Directly assigning raw address and some initilization of the chip//
// Interrupt all set to 1 - all masked//
TMP117::TMP117(uint8_t addr) {
    i2caddr = addr;
    
}

void TMP117::Initialise() {
    Serial.println("Started tmp config");
    // We first begin by writing three bytes to it 
    // First byte is the pointer register
    // Second byte is the MSB of the config register
    // Third byte is the LSB of the config register
    // We will write 0x03 to the pointer register to tell it we want to write to the config register
    Wire1.beginTransmission(i2caddr);
    Wire1.write(0x03); //Write to tell it we want to get the config register.
    Wire1.write(B00000010); //DEfault config
    Wire1.write(B01100000); //Default aside from us asking it to average 64 conversions instead of 8.

    Wire1.endTransmission(true);
    
    // // Now read back what we wrote to the config register to see if it worked.
    // Wire1.beginTransmission(i2caddr);
    // Wire1.write(0x03); //Write to tell it we want to get the config register.
    // Wire1.endTransmission(false); //false holds open the frame i.e. doesnt send stop command
    // // Now we want to read back two bytes
    // delayMicroseconds(50);  // Add small delay

    // Wire1.requestFrom(i2caddr, (uint8_t)2);
    // byte config1 = Wire1.read();
    // byte config2 = Wire1.read();
   
}

float TMP117::getTemp(){
    return latest_temperature;
}

void TMP117::updateTemp(){
    Wire1.beginTransmission(i2caddr);
    Wire1.write(0x00); //Write to tell it we want to get the temp_result register
    Wire1.endTransmission(false); //false holds open the frame i.e. doesnt send stop command
    // Now we want to read back two bytes
    // delayMicroseconds(10);  // Add small delay

    Wire1.requestFrom(i2caddr, (uint8_t)2); // Ask for two bytes
    byte byte1 = Wire1.read(); // MSB
    byte byte2 = Wire1.read(); // LSB
    //Now we need to convert our 16 bit which is currently twos complement
    int16_t raw_data = (byte1 << 8) | byte2;
    
    if (raw_data != 0x8000){ // on reset it will have a value of 0x8000
        latest_temperature = (float) raw_data / 128.0; // This is conversion as per Table 7-1 of the datasheet.
    }
    
    // teensy_device.sendDebug("TMP117 Temp: %.1f C, Raw: %d", latest_temperature, raw_data);
}
