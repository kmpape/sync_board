#pragma once
#include <Arduino.h>

// SyncBoard on-board IO: level-shifted GPIOs, fixed digital in/outs, the
// 12 V power switches, and one-shot ADC/DAC access.

namespace io {

constexpr int kNumGpios = 9;

// GPIO configuration, applied on the next system enable.
// A GPIO is identified by its board label (13, 25..32) or index (0..8).
// Returns -1 and faults if the label is not recognised.
int gpioIndexFromLabel(int label);
void setGpioConfig(int index, bool enabled, bool isInput);
void resetGpioConfig();  // everything disabled / high-impedance

// Live GPIO access (system must be enabled and the pin configured).
void writeGpio(int index, bool high);   // faults unless configured as output
bool readGpio(int index);               // faults unless configured as input

// Used by the signal engine: force a GPIO to output mode in the stored
// config and return its Teensy pin number (-1 on bad label).
int claimGpioAsOutput(int label);

// Fixed digital IO, channels 1..4.
bool readDigitalIn(int channel);
void writeDigitalOut(int channel, bool high);
int digitalOutPin(int channel);         // Teensy pin for a DO channel, -1 if bad

// Drive a DO high for durationMs, turned off by tickDoPulses().
void pulseDigitalOut(int channel, float durationMs);
void tickDoPulses();

// One-shot ADC read in volts. channel 1..8; adcId 0=SyncBoard, 1=LED board,
// 2=magnet board. Returns a negative value on fault.
float readAdcVolts(int channel, int adcId);

// On-board SPI DAC, channel 0 (all) or 1..8, output 0..3.3 V.
void setDacVolts(int channel, float volts);

// 12 V power switches, channel 0 (all, off only) or 1..16, duty 0..1.
void setSwitch(int channel, float duty);

// Bus + pin bring-up / safe-state. activate=true configures everything for
// operation; false puts all pins and expansion chips into a safe idle state.
void configure(bool activate);

}  // namespace io
