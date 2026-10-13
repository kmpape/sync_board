#ifndef _AD5675_H_
#define _AD5675_H_
#include <Arduino.h>
#include <Wire.h>  //includes wire library//

class AD5675 {
 private:
  uint8_t i2caddr;
  uint16_t GenerateRef(float voltage); // generates the 16bit reference number relating to the requested voltage

 public:
  AD5675();
  AD5675(uint8_t addr);  // intialize using address
  void Initialise();
  void SetVoltage(uint8_t channel, float voltage);  // sets the voltage on the DAC channel
};

#define REG_WriteVoltage 0x3
#define AD5675_MaxVoltage 4.096 // update on machine.h if changed here

#endif