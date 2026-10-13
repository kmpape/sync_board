#ifndef _AD5673_H_
#define _AD5673_H_
#include <Arduino.h>
#include <Wire.h>  //includes wire library//
#include "Teensy_HID.h"

class AD5673 {
 private:
  uint8_t i2caddr;
  uint16_t GenerateRef(float voltage); // generates the 16bit reference number relating to the requested voltage

 public:
  AD5673();
  AD5673(uint8_t addr);  // intialize using address
  void Initialise();
  uint8_t PinToChannel(uint8_t pin);  // converts the pin number to the channel number
  void SetVoltage(uint8_t pin, float voltage);  // sets the voltage on the DAC pin
};

#define COMMAND_RESET 0x60 // 0b0110 0000
#define RESET_HB 0x12
#define RESET_LB 0x34

#define REG_WriteVoltage 0x3
#define AD5673_MaxVoltage 5.0

#endif