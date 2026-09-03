#include "Leds.h"

#include "Calibration.h"
#include "Chips.h"
#include "Io.h"
#include "Pins.h"
#include "Protocol.h"
#include "State.h"
#include "digitalWriteFast.h"

namespace leds {
namespace {

// Current sense: 3 mOhm shunt into a gain-50 amplifier, so ADC volts per amp.
constexpr float kCurrentGainVPerA = 0.003f * 50.0f;
// Fraction of the user's max current actually mapped to level 1.0.
constexpr float kMaxCurrentUsed = 0.95f;
// Empirical turn-on/settling delay, used to make short exposures accurate.
constexpr uint32_t kLedDelayUs = 5;
// Levels above this may not stay on without a timer (heat risk).
constexpr float kMaxUntimedLevel = 0.3f;

// Which signal the ADC-readback mux (per channel) currently routes:
// true = current sense, false = optical power monitor.
bool outputIsCurrent[kNumLeds];
// Which signal the analog control loop regulates.
bool feedbackIsCurrent[kNumLeds];

float levelSet[kNumLeds] = {0.0f};

uint32_t timedEndUs[kNumLeds] = {0};
bool timedActive[kNumLeds] = {false};
int numTimedActive = 0;

bool validChannel(int channel) {
  if (channel < 1 || channel > kNumLeds) {
    protocol::fault("LED channel %d out of range 1..8", channel);
    return false;
  }
  return true;
}

// Selects what the per-channel ADC readback pin carries (signal DB).
// PWM channels 9..16 on the LED board's PCA9685.
void setOutputMux(int channel, bool current) {
  outputIsCurrent[channel - 1] = current;
  chips::pca9685SetDuty(chips::kLedPwmAddr, channel + 8, current ? 0.0f : 1.0f);
}

// Selects which signal the control loop regulates (signal DA).
// PWM channels 1..8.
void setFeedbackMux(int channel, bool current) {
  feedbackIsCurrent[channel - 1] = current;
  chips::pca9685SetDuty(chips::kLedPwmAddr, channel, current ? 0.0f : 1.0f);
}

float readOutput(int channel, bool current) {
  if (outputIsCurrent[channel - 1] != current) setOutputMux(channel, current);
  return io::readAdcVolts(channel, 1);
}

// Sets the raw DAC setpoint voltage for a channel.
void setRawVolts(int channel, float volts) {
  if (volts < 0.0f) volts = 0.0f;
  if (volts > 3.2f) {
    // 3.2 V leaves headroom below the 3V3 rail for the sense circuits.
    protocol::fault("LED raw setpoint above 3.2 V; the calibrated range should never reach here");
    volts = 3.2f;
  }
  chips::ad5669Set(chips::kLedDacAddr, channel, volts);
}

void switchRaw(int channel, bool on) {
  digitalWriteFast(pins::kLedEnable[channel - 1], on ? HIGH : LOW);
}

void cancelTimed(int channel) {
  if (timedActive[channel - 1]) {
    timedActive[channel - 1] = false;
    numTimedActive--;
  }
}

float avgAdcReads(int channel, bool current, int count) {
  float sum = 0.0f;
  for (int i = 0; i < count; i++) sum += readOutput(channel, current);
  return sum / (float)count;
}

// Least-squares fit y = slope*x + intercept over points [from, n).
void linearFit(const float* x, const float* y, int from, int n,
               float& slope, float& intercept) {
  float sx = 0, sy = 0, sxx = 0, sxy = 0;
  const int m = n - from;
  for (int i = from; i < n; i++) {
    sx += x[i];
    sy += y[i];
    sxx += x[i] * x[i];
    sxy += x[i] * y[i];
  }
  slope = (m * sxy - sx * sy) / (m * sxx - sx * sx);
  intercept = (sy - slope * sx) / m;
}

void heartbeatWiggle(int delayMs) {
  digitalWriteFast(pins::kHeartbeat, LOW);
  delay(delayMs);
  digitalWriteFast(pins::kHeartbeat, HIGH);
  delay(delayMs);
  digitalWriteFast(pins::kHeartbeat, LOW);
}

}  // namespace

void init() { calibration::load(); }

float levelOf(int channel) { return levelSet[channel - 1]; }

void setLevel(int channel, float level, bool currentFeedback) {
  if (!validChannel(channel)) return;
  if (level < 0.0f || level > 1.0f) {
    protocol::fault("LED level %.3f out of range 0..1", (double)level);
    return;
  }
  if (feedbackIsCurrent[channel - 1] != currentFeedback) {
    setFeedbackMux(channel, currentFeedback);
  }

  float volts = 0.0f;
  if (level > 0.0f) {
    const calibration::LedCal& cal = calibration::led(channel);
    if (currentFeedback) {
      if (!cal.validCurrent()) {
        protocol::fault("LED %d is not calibrated; run calibrateLed first", channel);
        return;
      }
      volts = cal.turnOnVCurrent + level * (cal.maxVCurrent - cal.turnOnVCurrent);
    } else {
      if (!cal.validPower()) {
        protocol::fault(
            "LED %d has no optical calibration (uncalibrated, or no optical sensor)", channel);
        return;
      }
      volts = cal.turnOnVPower + level * (cal.maxVPower - cal.turnOnVPower);
    }
  }
  setRawVolts(channel, volts);
  levelSet[channel - 1] = level;
}

void switchOn(int channel, bool on, bool force) {
  if (!validChannel(channel)) return;
  if (on && !force && levelSet[channel - 1] > kMaxUntimedLevel) {
    protocol::fault(
        "refusing to leave LED %d on untimed above %.0f%% power (heat risk if the host "
        "crashes); use switchLedTimed",
        channel, (double)(kMaxUntimedLevel * 100.0f));
    return;
  }
  switchRaw(channel, on);
  if (!on) cancelTimed(channel);
}

void switchTimed(int channel, float durationMs) {
  if (!validChannel(channel)) return;
  // The 30 min cap keeps the end-time comparison safely inside the 32-bit
  // micros() wrap window.
  if (durationMs <= 0.0f || durationMs > 30.0f * 60.0f * 1000.0f) {
    protocol::fault("LED on-time %.1f ms out of range (0, 30 min]", (double)durationMs);
    return;
  }
  if (timedActive[channel - 1]) {
    protocol::fault("LED %d is already on a timer", channel);
    return;
  }
  if (levelSet[channel - 1] > kMaxUntimedLevel && durationMs > 10000.0f) {
    protocol::fault(
        "refusing to run LED %d above %.0f%% power for more than 10 s (heat risk)",
        channel, (double)(kMaxUntimedLevel * 100.0f));
    return;
  }
  if (levelSet[channel - 1] == 0.0f) {
    protocol::logf("WARNING: LED %d turned on with level 0", channel);
  }

  const uint32_t durationUs = (uint32_t)(durationMs * 1000.0f) + kLedDelayUs;
  switchRaw(channel, true);
  const uint32_t startUs = micros();

  if (durationUs < 100) {
    // Too short for the main loop to time accurately; busy-wait inline.
    delayMicroseconds(durationUs);
    switchRaw(channel, false);
    return;
  }
  timedActive[channel - 1] = true;
  timedEndUs[channel - 1] = startUs + durationUs;
  numTimedActive++;
}

void allOff() {
  for (int i = 0; i < kNumLeds; i++) {
    digitalWriteFast(pins::kLedEnable[i], LOW);
    timedActive[i] = false;
  }
  numTimedActive = 0;
}

void reset(bool hardwareActive) {
  allOff();
  for (int ch = 1; ch <= kNumLeds; ch++) {
    if (hardwareActive && gLedAttached) {
      setFeedbackMux(ch, true);
      setOutputMux(ch, true);
      setRawVolts(ch, 0.0f);
    } else {
      // Keep the RAM mirrors consistent with the chips' power-on defaults.
      feedbackIsCurrent[ch - 1] = true;
      outputIsCurrent[ch - 1] = true;
    }
    levelSet[ch - 1] = 0.0f;
  }
}

bool anyTimedActive() { return numTimedActive > 0; }

void tick() {
  if (numTimedActive == 0) return;
  const uint32_t now = micros();
  for (int i = 0; i < kNumLeds; i++) {
    if (timedActive[i] && (int32_t)(now - timedEndUs[i]) >= 0) {
      switchRaw(i + 1, false);
      timedActive[i] = false;
      numTimedActive--;
    }
  }
}

void measure(int channel, float result[2]) {
  result[0] = -1.0f;
  result[1] = -1.0f;
  if (!validChannel(channel)) return;
  const calibration::LedCal& cal = calibration::led(channel);
  if (!cal.validCurrent()) {
    protocol::fault("LED %d is not calibrated; zero references are unknown", channel);
    return;
  }
  const float currentV = avgAdcReads(channel, true, 50);
  result[0] = (currentV - cal.zeroCurrent) / kCurrentGainVPerA;
  const float powerV = avgAdcReads(channel, false, 50);
  result[1] = (powerV - cal.zeroPower) * 1000.0f;
}

float measurePhotodiode(int channel) {
  if (!validChannel(channel)) return 0.0f;
  const float volts = readOutput(channel, false);
  // Generic 0.3 V dark-level offset; not individually calibrated.
  return (volts - 0.3f) * 1000.0f;
}

void getSetup(int channel, float result[4]) {
  if (!validChannel(channel)) return;
  const calibration::LedCal& cal = calibration::led(channel);
  const float level = levelSet[channel - 1];
  result[0] = level;
  if (level == 0.0f) {
    result[1] = result[2] = result[3] = 0.0f;
    protocol::fault("LED %d has no level set; nothing meaningful to report", channel);
    return;
  }
  result[1] = (cal.maxVCurrent - cal.turnOnVCurrent) * level / kCurrentGainVPerA;
  result[2] = (cal.maxVPower - cal.turnOnVPower) * level * 1000.0f;
  result[3] = (cal.maxVCurrent - cal.turnOnVCurrent) / kCurrentGainVPerA;
}

void calibrate(int channel, float maxCurrentA) {
  if (!validChannel(channel)) return;

  constexpr int kNumPoints = 40;
  static float measCurrent[kNumPoints];
  static float measPower[kNumPoints];

  setFeedbackMux(channel, true);  // calibrate under current feedback

  // ---- Phase 1: find the turn-on voltage (expected near 0.3 V) ------------
  float startV = 0.25f;
  float endV = 0.35f;
  for (int i = 0; i < kNumPoints; i++) {
    const float v = startV + (endV - startV) * i / (kNumPoints - 1);
    setRawVolts(channel, v);
    switchRaw(channel, true);
    heartbeatWiggle(3);  // keeps expansion boards alive + settling time
    measCurrent[i] = avgAdcReads(channel, true, 20);
    measPower[i] = avgAdcReads(channel, false, 20);
    switchRaw(channel, false);
  }

  // Baseline statistics over the first quarter, assumed fully off.
  const int nBase = kNumPoints / 4;
  float zeroCurrent = 0, zeroPower = 0;
  float cMin = measCurrent[0], cMax = measCurrent[0];
  float pMin = measPower[0], pMax = measPower[0];
  for (int i = 0; i < nBase; i++) {
    zeroCurrent += measCurrent[i];
    zeroPower += measPower[i];
    if (measCurrent[i] < cMin) cMin = measCurrent[i];
    if (measCurrent[i] > cMax) cMax = measCurrent[i];
    if (measPower[i] < pMin) pMin = measPower[i];
    if (measPower[i] > pMax) pMax = measPower[i];
  }
  zeroCurrent /= nBase;
  zeroPower /= nBase;
  const float onThreshold = (cMax - cMin) + zeroCurrent;

  int turnOnIndex = -1;
  for (int i = 0; i < kNumPoints; i++) {
    const float v = startV + (endV - startV) * i / (kNumPoints - 1);
    if (measCurrent[i] > onThreshold && v > 0.3f) {
      turnOnIndex = i;
      break;
    }
  }
  if (turnOnIndex < 0 || turnOnIndex >= kNumPoints - 2) {
    protocol::fault(
        "LED %d calibration failed: no turn-on detected below %.2f V. Is a LED connected "
        "to that channel?",
        channel, (double)endV);
    return;
  }

  // Fit the on-region and intersect with the off-baseline to place turn-on
  // precisely. Voltages regenerated from the sweep parameters.
  static float sweepV[kNumPoints];
  for (int i = 0; i < kNumPoints; i++) {
    sweepV[i] = startV + (endV - startV) * i / (kNumPoints - 1);
  }
  float slope, intercept;
  linearFit(sweepV, measCurrent, turnOnIndex, kNumPoints, slope, intercept);
  const float turnOnVCurrent = (zeroCurrent - intercept) / slope;
  linearFit(sweepV, measPower, turnOnIndex, kNumPoints, slope, intercept);
  float turnOnVPower = (zeroPower - intercept) / slope;

  // ---- Phase 2: ramp to the max allowed current ---------------------------
  startV = turnOnVCurrent + 0.03f;  // clear of the turn-on knee
  endV = turnOnVCurrent + maxCurrentA * kMaxCurrentUsed * kCurrentGainVPerA;

  float measPowerMax = 0.0f;
  for (int i = 0; i < kNumPoints; i++) {
    const float v = startV + (endV - startV) * i / (kNumPoints - 1);
    setRawVolts(channel, v);
    switchRaw(channel, true);
    heartbeatWiggle(1);
    measCurrent[i] = avgAdcReads(channel, true, 10);
    measPower[i] = avgAdcReads(channel, false, 10);
    if (measPower[i] > measPowerMax) measPowerMax = measPower[i];
    switchRaw(channel, false);
  }

  // Under current feedback the readback tracks the setpoint, so a reading
  // collapsing below the ramp start means the hardware over-current
  // protection cut the channel off.
  int limitIndex = -1;
  for (int i = 2; i < kNumPoints; i++) {
    if (measCurrent[i] < startV) {
      limitIndex = i;
      break;
    }
  }

  int lastGood = kNumPoints - 1;
  if (limitIndex >= 0) {
    lastGood = limitIndex - 1;
    if (limitIndex < kNumPoints - 2) {
      const float vTrip = startV + (endV - startV) * limitIndex / (kNumPoints - 1);
      protocol::logf(
          "WARNING: LED %d tripped its hardware over-current protection near %.3f A; "
          "calibration truncated there. Lower maxCurrent or change the limiter resistors.",
          channel, (double)((vTrip - turnOnVCurrent) / kCurrentGainVPerA));
    }
  }
  float maxVCurrent = measCurrent[lastGood];
  float maxVPower = measPower[lastGood];

  if (measPowerMax < 0.32f) {
    // No optical sensor response: forbid optical feedback by zeroing its cal.
    protocol::logf(
        "WARNING: LED %d shows no optical sensor response; optical feedback disabled "
        "for this channel.",
        channel);
    turnOnVPower = 0.0f;
    maxVPower = 0.0f;
    zeroPower = 0.0f;
  }

  calibration::LedCal& cal = calibration::led(channel);
  cal.turnOnVCurrent = turnOnVCurrent;
  cal.maxVCurrent = maxVCurrent;
  cal.zeroCurrent = zeroCurrent;
  cal.turnOnVPower = turnOnVPower;
  cal.maxVPower = maxVPower;
  cal.zeroPower = zeroPower;
  calibration::storeLed(channel);

  levelSet[channel - 1] = 0.0f;
  setRawVolts(channel, 0.0f);
}

}  // namespace leds
