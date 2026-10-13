#include "AD5673.h"
#include "Teensy_HID.h"

AD5673::AD5673() : i2caddr(0){
    teensy_device.sendError("Bad key provided for AD5673");
}

AD5673::AD5673(uint8_t addr) : i2caddr(addr){
}

void AD5673::Initialise(){
    // Initialise the DAC

    uint8_t result = 99;
    // Wire1.beginTransmission(i2caddr);
    // Wire1.write(0b01000000); // power on for channels 0-7
    // Wire1.write(0x00); // set all channels to normal operation
    // Wire1.write(0x00); // set all channels to normal operation
    // result = Wire1.endTransmission();
    // if (result != 0) {
    //     teensy_device.sendError("Power-up of AD5673 0-7 failed, Error code: %d", result);
    //     delay(100); // wait before retrying
    // }
    
    // Wire1.beginTransmission(i2caddr);
    // Wire1.write(0b01001000); // power on for channels 8-15
    // Wire1.write(0x00); // set all channels to normal operation
    // Wire1.write(0x00); // set all channels to normal operation
    // result = Wire1.endTransmission();
    // if (result != 0) {
    //     teensy_device.sendError("Power-up of AD5673 8-15 failed, Error code: %d", result);
    //     delay(100); // wait before retrying
    // }

    // software reset
    result = 99;
    uint8_t num_tries_remaining = 1;
    while ((result != 0) && (num_tries_remaining > 0)) {
        Wire1.beginTransmission(i2caddr);
        Wire1.write(COMMAND_RESET); // command byte for software reset
        Wire1.write(RESET_LB); // data byte 1 for software reset
        Wire1.write(RESET_HB); // data byte 2 for software reset
        result = Wire1.endTransmission();
        if (result != 0) {
            teensy_device.sendError("Setup of AD5673 failed, Error code: %d. Retrying", result);
            num_tries_remaining--;
            delay(100); // wait before retrying
        }
    }
    // THIS should turn everythhing to the default safe/startup state
    // turn stirring on to any speed
    // call initialise 
    // should now be 0% power so should stop moving 
}

uint16_t AD5673::GenerateRef(float voltage) {
  // Description:
  // voltage is the voltage to convert to a reference number
  // returns the reference number to set the voltage, it is an integer between 0-4096 and represents a voltage between 0-5V

    // 0-5V = 0-4095
    // 5-2 / 5 = 4095 - x / 4095
    // x = 4095 - ((4095 * (DAC_MaxVoltage - voltage)) / DAC_MaxVoltage)
    return (uint16_t)(4095 - ((4095 * (AD5673_MaxVoltage - voltage)) / AD5673_MaxVoltage));
}

uint8_t AD5673::PinToChannel(uint8_t pin) {
    // Description:
    // pin is the pin number on the DAC and valid numbers are 1, 22-28, 8-14, 7
    if (pin == 1) {
        return 0;
    } else if (pin == 22) {
        return 11;
    } else if (pin == 23) {
        return 10;
    } else if (pin == 24) {
        return 9;
    } else if (pin == 25) {
        return 8;
    } else if (pin == 26) {
        return 3;
    } else if (pin == 27) {
        return 2;
    } else if (pin == 28) {
        return 1;
    } else if (pin == 8 ){
        return 6;
    } else if (pin == 9 ){
        return 5;
    } else if (pin == 10) {
        return 4;
    } else if (pin == 11) {
        return 15;
    } else if (pin == 12) {
        return 14;
    } else if (pin == 13) {
        return 13;
    } else if (pin == 14) {
        return 12;
    } else if (pin == 7 ){
        return 7;
    } else {
        teensy_device.sendError("Have not selected a valid pin number for the Heatboard Stir DAC");
        return 0xFF;
    }
}

void AD5673::SetVoltage(uint8_t pin, float voltage) {
    // Description:
    // pin is the pin number on the DAC and valid numbers are betweren 0 and 7 inclusive
    // voltage is the requested voltage in V to set the DAC pin to

    uint16_t ref = GenerateRef(voltage);

    uint8_t ref_msb = (ref & 0x0FF0) >> 4;
    uint8_t ref_lsb = (ref & 0x000F) << 4;
    // example:
    // we want 2V
    // 0-5V = 0-4095
    // 5-2 / 5 = 4095 - x / 4095
    // x = 4095 - ((4095 * (5-2)) / 5) = 1638
    // 1638 in 16 bit binary = 0b0000011001100110
    // (0b0000 0110 0110 0110 & 0x0FF0) >> 4 = 0b0000 0110 0110 0000 >> 4 = 0b0110 0110
    // (0b0000 0110 0110 0110 & 0x000F) << 4 = 0b0000 0000 0000 0110 << 4 = 0b0110 0000

    // pin is a number between 0-15 but is represented by 8 bits, we only use
    // the 4 lower bits
    uint8_t channel = PinToChannel(pin);
    uint8_t command = (REG_WriteVoltage << 4) | channel;
    Wire1.beginTransmission(i2caddr);
    Wire1.write(command);
    Wire1.write(ref_msb);
    Wire1.write(ref_lsb);

    // End the transmission
    uint8_t result = Wire1.endTransmission();
    // Check for acknowledgment or errors
    if (result != 0)
    {
        teensy_device.sendError("Heatboard DAC I2C failed: %d", result);
    }
}