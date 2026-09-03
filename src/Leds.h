#pragma once
#include <Arduino.h>

// LED board driver: 8 channels, each with an enable line (Teensy pin), a DAC
// setpoint, a current/optical feedback mode switch, and ADC readback.
//
// Calibration maps a user-facing "level" in [0,1] onto the DAC voltage range
// [turn-on voltage, voltage at max allowed current] and is persisted to
// EEPROM (see Calibration.h). Channels are 1..8 throughout.

namespace leds {

constexpr int kNumLeds = 8;

// Loads persisted calibration into RAM. Call once at boot.
void init();

// Feedback selection: true = constant current, false = constant optical power.
// Set the level (0..1, calibrated) and the feedback mode used to hold it.
// Faults if the channel is uncalibrated for the requested mode.
void setLevel(int channel, float level, bool currentFeedback);
float levelOf(int channel);

// Plain on/off. Turning on a LED whose level exceeds 30% of max is refused
// (heat risk if the host crashes); use switchTimed for high power.
void switchOn(int channel, bool on, bool force = false);

// On for durationMs, turned off by tick(). Pulses < 100 us are timed inline.
void switchTimed(int channel, float durationMs);

// Turn off every enable line unconditionally (safe even with no LED board).
void allOff();

// Reset driver state and (if hardwareActive) reprogram the board's chips to
// defaults. Called on system enable/disable.
void reset(bool hardwareActive);

// Turns any expired timed LEDs off; call every loop iteration.
void tick();
bool anyTimedActive();

// Measures a channel: result[0] = current in A, result[1] = optical power
// monitor in mV. Averaged over many ADC reads, so takes tens of ms.
void measure(int channel, float result[2]);

// One-shot photodiode read in mV (uncalibrated, ~0 in darkness).
float measurePhotodiode(int channel);

// result = {level, currentA at that level, optical mV at that level,
//           max allowed current A}. Faults if level/calibration unset.
void getSetup(int channel, float result[4]);

// Sweeps the channel to find its turn-on voltage and the voltage reaching
// maxCurrentA, in both feedback modes, and persists the result. Blocks for
// several seconds and flashes the LED.
void calibrate(int channel, float maxCurrentA);

}  // namespace leds
