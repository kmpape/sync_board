#ifndef _PCA9685_H_
#define _PCA9685_H_
#include <Arduino.h>
#include <Wire.h>  //includes wire library//
#include "Teensy_HID.h"
#include "I2CAddressBook.h"

// each LED has 4 bits to control the output and brightness

class PCA9685 {
  public:
    PCA9685();
    PCA9685(uint8_t addr);  // intialize using address
    void Initialise();
    void EnableExternalClock();
    void Sleep();
    void Wake();
    void SetFrequency(uint16_t freq, uint32_t clk_freq = 1843200);
    void SetPWM(uint8_t pin, uint16_t on, uint16_t off);
    void SetPWM(uint8_t pin, uint16_t duty_cycle_12bit);
    void SetAllPWM(uint16_t duty_cycle_12bit);
  private:
    // the i2c address
    uint8_t i2caddr;
    uint16_t wakeup_delay_ms = 500;  // sets the delay in ms for the oscillator to
                                     // stabilize after waking up
    void AutoIncr(bool state);
    bool ExternalClock = false;
    uint8_t getChannel(uint8_t pin_number);
};

#endif
