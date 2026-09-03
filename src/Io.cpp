#include "Io.h"

#include <SPI.h>
#include <Wire.h>

#include "Chips.h"
#include "Pins.h"
#include "Protocol.h"
#include "State.h"
#include "digitalWriteFast.h"

namespace io {
namespace {

struct GpioInfo {
  int label;              // silk-screen name: GPIO<label>
  int teensyPin;
  int levelShiftDirCh;    // PCA9685 channel of the direction pin; -1 = none
};

// GPIO29/30 have no level shifter (they double as analog inputs). For the
// rest, the enable pin sits on the PWM channel after the direction pin.
constexpr GpioInfo kGpios[kNumGpios] = {
    {13, 30, 1}, {25, 22, 3}, {26, 23, 5}, {27, 9, 7}, {28, 24, 9},
    {29, 16, -1}, {30, 17, -1}, {31, 14, 11}, {32, 15, 13},
};

bool gpioEnabled[kNumGpios] = {false};
bool gpioIsInput[kNumGpios] = {true};

bool bulkShiftersEnabled = false;

uint32_t doPulseEndUs[4] = {0};
bool doPulseActive[4] = {false};

// Applies one GPIO's stored config to the pin and its level shifter.
void applyGpioPin(int index) {
  const GpioInfo& g = kGpios[index];
  const int dirCh = g.levelShiftDirCh;
  if (!gpioEnabled[index]) {
    pinModeFast(g.teensyPin, INPUT);  // high impedance
    if (dirCh > 0) {
      chips::pca9685SetDuty(chips::kSyncPwmLevelShiftAddr, dirCh + 1, 0.0f);  // shifter off
      chips::pca9685SetDuty(chips::kSyncPwmLevelShiftAddr, dirCh, 0.0f);      // input direction
    }
    return;
  }
  if (gpioIsInput[index]) {
    pinModeFast(g.teensyPin, INPUT);
    if (dirCh > 0) {
      chips::pca9685SetDuty(chips::kSyncPwmLevelShiftAddr, dirCh, 0.0f);
      chips::pca9685SetDuty(chips::kSyncPwmLevelShiftAddr, dirCh + 1, 1.0f);
    }
  } else {
    pinModeFast(g.teensyPin, OUTPUT);
    digitalWriteFast(g.teensyPin, LOW);
    if (dirCh > 0) {
      chips::pca9685SetDuty(chips::kSyncPwmLevelShiftAddr, dirCh, 1.0f);
      chips::pca9685SetDuty(chips::kSyncPwmLevelShiftAddr, dirCh + 1, 1.0f);
    }
  }
}

void enableBulkShifters(bool enable) {
  if (enable == bulkShiftersEnabled) return;
  // Channel 15 is En7 (active low: output enable of the bulk shifters),
  // channel 16 is En8 (powers the 3V3S net that feeds them).
  chips::pca9685SetDuty(chips::kSyncPwmLevelShiftAddr, 15, enable ? 0.0f : 1.0f);
  chips::pca9685SetDuty(chips::kSyncPwmLevelShiftAddr, 16, enable ? 1.0f : 0.0f);
  bulkShiftersEnabled = enable;
}

// Puts every expansion chip output into its off state. Relies on "0 output"
// meaning "off" throughout the PCB.
void turnChipsOff() {
  chips::pca9685SetDuty(chips::kSyncPwmLevelShiftAddr, 0, 0.0f);
  chips::pca9685SetDuty(chips::kSyncPwmLevelShiftAddr, 15, 1.0f);  // bulk shifters off
  chips::pca9685SetDuty(chips::kSyncPwmSwitchesAddr, 0, 0.0f);
  chips::ad5668Setup(false);
  if (gLedAttached) {
    chips::pca9685SetDuty(chips::kLedPwmAddr, 0, 0.0f);
    chips::ad5669Setup(chips::kLedDacAddr, false);
  }
  if (gMagnetAttached) {
    chips::ad5669Setup(chips::kMagDacAddr, false);
  }
}

void heartbeatWiggle(int delayMs) {
  // A few manual heartbeat edges keep the expansion boards' liveness
  // detectors happy while the main loop (and its heartbeat timer) is blocked.
  pinModeFast(pins::kHeartbeat, OUTPUT);
  digitalWriteFast(pins::kHeartbeat, LOW);
  delay(delayMs);
  digitalWriteFast(pins::kHeartbeat, HIGH);
  delay(delayMs);
  digitalWriteFast(pins::kHeartbeat, LOW);
}

}  // namespace

int gpioIndexFromLabel(int label) {
  if (label >= 0 && label < kNumGpios) return label;  // already an index
  for (int i = 0; i < kNumGpios; i++) {
    if (kGpios[i].label == label) return i;
  }
  protocol::fault("unknown GPIO '%d' (use 13, 25..32 or index 0..8)", label);
  return -1;
}

void setGpioConfig(int index, bool enabled, bool isInput) {
  gpioEnabled[index] = enabled;
  gpioIsInput[index] = isInput;
}

void resetGpioConfig() {
  for (int i = 0; i < kNumGpios; i++) {
    gpioEnabled[i] = false;
    gpioIsInput[i] = true;
  }
}

void writeGpio(int index, bool high) {
  if (!gpioEnabled[index]) {
    protocol::fault("GPIO index %d is not enabled", index);
    return;
  }
  if (gpioIsInput[index]) {
    protocol::fault("GPIO index %d is configured as an input", index);
    return;
  }
  digitalWriteFast(kGpios[index].teensyPin, high ? HIGH : LOW);
}

bool readGpio(int index) {
  if (!gpioEnabled[index]) {
    protocol::fault("GPIO index %d is not enabled", index);
    return false;
  }
  if (!gpioIsInput[index]) {
    protocol::fault("GPIO index %d is configured as an output", index);
    return false;
  }
  return digitalReadFast(kGpios[index].teensyPin);
}

int claimGpioAsOutput(int label) {
  const int index = gpioIndexFromLabel(label);
  if (index < 0) return -1;
  setGpioConfig(index, true, false);
  // If the system is already up, make the pin an output right now; otherwise
  // the next enable applies it.
  if (gSystemEnabled) applyGpioPin(index);
  return kGpios[index].teensyPin;
}

bool readDigitalIn(int channel) {
  if (channel < 1 || channel > 4) {
    protocol::fault("digital input channel %d out of range 1..4", channel);
    return false;
  }
  return digitalReadFast(pins::kDigitalIn[channel - 1]);
}

int digitalOutPin(int channel) {
  if (channel < 1 || channel > 4) {
    protocol::fault("digital output channel %d out of range 1..4", channel);
    return -1;
  }
  return pins::kDigitalOut[channel - 1];
}

void writeDigitalOut(int channel, bool high) {
  const int pin = digitalOutPin(channel);
  if (pin < 0) return;
  digitalWriteFast(pin, high ? HIGH : LOW);
}

void pulseDigitalOut(int channel, float durationMs) {
  const int pin = digitalOutPin(channel);
  if (pin < 0) return;
  // The 60 s cap keeps the end-time comparison safely inside the 32-bit
  // micros() wrap window; pulses are for triggering, not slow control.
  if (durationMs <= 0.0f || durationMs > 60000.0f) {
    protocol::fault("pulse duration %.1f ms out of range (0, 60000]", (double)durationMs);
    return;
  }
  if (doPulseActive[channel - 1]) {
    protocol::fault("digital output %d is already pulsing", channel);
    return;
  }
  digitalWriteFast(pin, HIGH);
  doPulseActive[channel - 1] = true;
  doPulseEndUs[channel - 1] = micros() + (uint32_t)(durationMs * 1000.0f);
}

void tickDoPulses() {
  const uint32_t now = micros();
  for (int i = 0; i < 4; i++) {
    if (doPulseActive[i] && (int32_t)(now - doPulseEndUs[i]) >= 0) {
      digitalWriteFast(pins::kDigitalOut[i], LOW);
      doPulseActive[i] = false;
    }
  }
}

float readAdcVolts(int channel, int adcId) {
  if (channel < 1 || channel > 8) {
    protocol::fault("ADC channel %d out of range 1..8", channel);
    return -1.0f;
  }
  uint8_t address;
  switch (adcId) {
    case 0: address = chips::kSyncAdcAddr; break;
    case 1:
      if (!gLedAttached) {
        protocol::fault("LED board not attached");
        return -1.0f;
      }
      address = chips::kLedAdcAddr;
      break;
    case 2:
      if (!gMagnetAttached) {
        protocol::fault("magnet board not attached");
        return -1.0f;
      }
      address = chips::kMagAdcAddr;
      break;
    default:
      protocol::fault("ADC id %d out of range 0..2", adcId);
      return -1.0f;
  }
  const uint16_t code = chips::ads7828Read(address, channel - 1);
  return code * 3.3f / 4096.0f;  // external 3.3 V reference, 12-bit
}

void setDacVolts(int channel, float volts) {
  if (channel < 0 || channel > 8) {
    protocol::fault("DAC channel %d out of range 0..8", channel);
    return;
  }
  if (volts < 0.0f || volts > 3.3f) {
    protocol::fault("DAC voltage %.3f out of range 0..3.3 V", (double)volts);
    return;
  }
  chips::ad5668Set(channel, volts);
}

void setSwitch(int channel, float duty) {
  if (channel < 0 || channel > 16) {
    protocol::fault("switch channel %d out of range 0..16", channel);
    return;
  }
  if (duty < 0.0f || duty > 1.0f) {
    protocol::fault("switch duty %.3f out of range 0..1", (double)duty);
    return;
  }
  if (channel == 0 && duty > 0.0f) {
    protocol::fault("turning all switches on at once is not allowed");
    return;
  }
  chips::pca9685SetDuty(chips::kSyncPwmSwitchesAddr, channel, duty);
}

void configure(bool activate) {
  heartbeatWiggle(1);

  if (activate) {
    for (int i = 0; i < 4; i++) {
      pinModeFast(pins::kDigitalIn[i], INPUT);
      pinModeFast(pins::kDigitalOut[i], OUTPUT);
      digitalWriteFast(pins::kDigitalOut[i], LOW);
      doPulseActive[i] = false;
      pinModeFast(pins::kCameraLed[i], INPUT);
    }
    pinModeFast(pins::kCameraTriggerReady, INPUT);
    pinModeFast(pins::kCameraReading, INPUT);
  } else {
    // Safe state: everything high impedance first.
    for (int i = 0; i < 42; i++) pinModeFast(i, INPUT);
  }

  SPI.begin();
  SPI.setMOSI(pins::kSpiMosi);
  SPI.setMISO(pins::kSpiMiso);
  SPI.setSCK(pins::kSpiSck);
  SPI.setBitOrder(MSBFIRST);
  pinModeFast(pins::kSpiCs0, OUTPUT);
  pinModeFast(pins::kSpiCs1, OUTPUT);

  Wire.begin();
  // 100 kHz: the bus spans multiple boards and has significant capacitance.
  Wire.setClock(100000L);

  chips::pca9685Init(chips::kSyncPwmSwitchesAddr, /*slowClock=*/true);
  chips::pca9685Init(chips::kSyncPwmLevelShiftAddr);
  if (gLedAttached) chips::pca9685Init(chips::kLedPwmAddr);
  bulkShiftersEnabled = false;  // matches what turnChipsOff() writes
  turnChipsOff();

  if (activate) {
    for (int i = 0; i < kNumGpios; i++) applyGpioPin(i);
  } else {
    for (int i = 0; i < kNumGpios; i++) {
      const bool enabledBefore = gpioEnabled[i];
      gpioEnabled[i] = false;
      applyGpioPin(i);
      gpioEnabled[i] = enabledBefore;
    }
  }

  heartbeatWiggle(3);

  // Pins that must not float regardless of mode.
  for (int i = 0; i < 8; i++) {
    pinModeFast(pins::kLedEnable[i], OUTPUT);
    digitalWriteFast(pins::kLedEnable[i], LOW);
  }
  pinModeFast(pins::kDacClrBar, OUTPUT);
  digitalWriteFast(pins::kDacClrBar, HIGH);
  pinModeFast(pins::kDacLdacBar, OUTPUT);
  digitalWriteFast(pins::kDacLdacBar, LOW);
  pinModeFast(pins::kCameraTriggerIn, OUTPUT);
  digitalWriteFast(pins::kCameraTriggerIn, LOW);

  if (activate) {
    enableBulkShifters(true);
    chips::ad5668Setup(true);
    if (gLedAttached) chips::ad5669Setup(chips::kLedDacAddr, true);
  } else {
    enableBulkShifters(false);
    SPI.end();
    Wire.end();
    pinMode(pins::kSpiCs0, INPUT_DISABLE);
    pinMode(pins::kSpiCs1, INPUT_DISABLE);
    pinMode(pins::kSpiMosi, INPUT_DISABLE);
    pinMode(pins::kSpiMiso, INPUT_DISABLE);
    pinMode(pins::kSpiSck, INPUT_DISABLE);
    pinMode(pins::kI2cSda, INPUT_DISABLE);
    pinMode(pins::kI2cScl, INPUT_DISABLE);
  }
}

}  // namespace io
