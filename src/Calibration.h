#pragma once
#include <Arduino.h>

// EEPROM-backed calibration storage.
//
// Layout: a 4-byte magic/version header followed by fixed structs. Changing
// any struct below requires bumping the magic, which invalidates stored
// calibrations (boards must simply be recalibrated after such a firmware
// update).

namespace calibration {

struct LedCal {
  float turnOnVCurrent;   // DAC volts where the LED starts conducting (current mode)
  float maxVCurrent;      // DAC volts at the max allowed current
  float zeroCurrent;      // ADC volts read with the LED off (current output)
  float turnOnVPower;     // as above, optical-power mode
  float maxVPower;
  float zeroPower;

  bool validCurrent() const { return turnOnVCurrent != 0.0f && maxVCurrent != 0.0f; }
  bool validPower() const { return turnOnVPower != 0.0f && maxVPower != 0.0f; }
};

// Loads all LED calibrations from EEPROM; missing/invalid storage yields
// zeroed (invalid) entries.
void load();

LedCal& led(int channel);            // channel 1..8
void storeLed(int channel);          // persist RAM entry for one channel

// Wipes the header and zeroes all calibration structs (RAM + EEPROM).
void factoryReset();

}  // namespace calibration
