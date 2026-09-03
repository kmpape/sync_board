#pragma once
#include <Arduino.h>

// Magnet board driver. The board carries an AD5669 DAC (relay drive + two
// current setpoints), an ADS7828 ADC (current monitor + Hall sensors), and is
// switched via two SyncBoard GPIO lines:
//   GPIO29 -> magnet enable, GPIO30 -> output select (LOW = NC, HIGH = NO).
//
// Current setpoints are only meaningful after calibrate() maps DAC volts to
// coil amps around the zero-current point; field setpoints additionally need
// calibrateHall().

namespace magnet {

// After system enable: probes the board's chips, powers up its DAC and
// claims the two GPIO control lines. Faults if a chip is missing.
void setup();

void enable(bool on);

// Selects which coil output the current flows through.
void selectOutput(bool nc);

// Maps DAC volts to measured coil current for both outputs. Blocks for
// several seconds and drives current through the coil.
void calibrate();

// Maps coil current to measured field of one Hall sensor (id 0..2).
void calibrateHall(int hallId);

// Setpoints. nc selects the output (true = NC). Faults when uncalibrated.
void setCurrent(bool nc, float amps);
void setField(bool nc, float milliTesla);

// Readback.
float readHallMilliTesla(int hallId);
float readAdcVolts(int channel);  // raw board ADC, channel 1..8

// Raw board DAC access (channel 1..8); mainly for the signal engine and
// bring-up tests. Faults if the board is absent or not set up.
void setDacVolts(int channel, float volts);

}  // namespace magnet
