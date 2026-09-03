#pragma once
#include <Arduino.h>

// Timed signal engine: up to kNumSignals independent sequences of
// (value, delay) steps executed from the main loop, used for AWG-style DAC
// output, timed ADC recording, digital pulse trains, LED gating, etc.
//
// A signal marked "slave" has no timebase of its own: it advances one step
// each time a CONDUCTOR signal whose option bitmask includes it fires, which
// keeps multiple outputs sample-locked to each other.

namespace signals {

constexpr int kNumSignals = 5;
constexpr int kMaxLength = 2000;

// Modes; numeric values are part of the wire protocol.
enum class Mode : uint8_t {
  kAdc = 0,           // record SyncBoard ADC channel <option>
  kDac = 1,           // write SyncBoard DAC channel <option>
  kGpioRead = 2,      // (not implemented)
  kMagDac = 3,        // write magnet-board DAC channel <option>
  kMagAdc = 4,        // record magnet-board ADC channel <option>
  kMagHallRead = 5,   // record Hall sensor <option> in mT
  kMagCurrent = 6,    // set magnet current, output NC=<option != 0>
  kMagField = 7,      // set magnet field in mT via Hall calibration
  kDo = 8,            // write digital output <option 1..4>
  kConductor = 9,     // advance slave signals in bitmask <option>
  kLed = 10,          // switch LED <option> on/off (low power only)
  kLedTimed = 11,     // pulse LED <option> for <value> ms (allows high power)
  kDoTimed = 12,      // pulse digital output <option> for <value> ms
  kGpioWrite = 13,    // write GPIO labelled <option>
};

// Configures a signal (must be inactive). Faults on invalid input.
void configure(int index, Mode mode, uint32_t option, bool repeat, bool isSlave);

// Loads the step table: count pairs of (value, delayMs). A delay of -1 ends
// the sequence early; delays are ignored for slave signals. For recording
// modes values are ignored. Must be inactive.
void load(int index, int count, const float* values, const float* delaysMs);

// Convenience for recording modes: count samples at a fixed interval.
void loadUniform(int index, int count, float intervalMs);

void start(int index);  // (re)starts from step 0; requires system enabled
void stop(int index);

void stopAll();     // deactivate + rewind everything (system disable path)
void resetAll();    // stopAll + clear all step tables

bool isActive(int index);
int length(int index);
const float* data(int index);

// Runs due signals; call every loop iteration while the system is enabled.
void tick();

bool validIndex(int index);  // faults when out of range

}  // namespace signals
