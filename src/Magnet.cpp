#include "Magnet.h"

#include "Chips.h"
#include "Io.h"
#include "LinearFit.h"
#include "Pins.h"
#include "Protocol.h"
#include "State.h"
#include "digitalWriteFast.h"

namespace magnet {
namespace {

// SyncBoard GPIO29 / GPIO30 route to the magnet header (Teensy pins).
constexpr int kEnablePin = 16;   // GPIO29: magnet enable
constexpr int kSelectPin = 17;   // GPIO30: output select, LOW = NC

// Magnet board DAC channels.
constexpr int kDacRelayCh = 1;
constexpr int kDacNoCh = 2;
constexpr int kDacNcCh = 3;
// Magnet board ADC channels.
constexpr int kAdcCurrentCh = 1;

// Hall sensor transfer: 6 mV/G behind a 1.5x divider; this zero-field
// voltage is a rough factory value, not individually calibrated.
constexpr float kHallZeroFieldV = 1.6765869f;

bool chipsReady = false;
bool magnetCalibrated = false;
bool hallCalibrated = false;

float zeroCurrentV = 1.6502f;
// Per-output current transfer: amps = slope * (dacV - interceptV).
float slopeNc = 1.0f, interceptNc = 1.65f;
float slopeNo = 1.0f, interceptNo = 1.65f;
// Field transfer: mT = hallSlope * amps + hallIntercept.
float hallSlope = 0.0f, hallIntercept = 0.0f;

constexpr int kCalPoints = 20;
constexpr int kCalAvg = 20;

bool requireReady() {
  if (!gMagnetAttached) {
    protocol::fault("magnet board not attached");
    return false;
  }
  if (!chipsReady) {
    protocol::fault("magnet board not set up; run setupMagnet after enabling the system");
    return false;
  }
  return true;
}

void setDac(int channel, float volts) {
  chips::ad5669Set(chips::kMagDacAddr, channel, volts);
}

// Relay closed connects the coil to the current driver.
void switchRelay(bool closed) { setDac(kDacRelayCh, closed ? 0.0f : 3.3f); }

// Current monitor reading relative to its zero-current reference. Units are
// "monitor volts", not amps: the setCurrent() setpoint scale is defined by
// this same reading, so the two cancel and only the Hall calibration ties
// anything to physical units.
float readCurrentMonitor() {
  return readAdcVolts(kAdcCurrentCh) - zeroCurrentV;
}

void heartbeatWiggle() {
  io::heartbeatPulse(3);
  delay(3);  // extra settling time between calibration points
}

// Sweeps one output's DAC around the zero-current point and fits
// amps-per-volt. Returns false (and faults) if the intercept is implausible.
bool calibrateOutput(int dacChannel, float& slope, float& interceptV) {
  constexpr float kStartV = 1.64f;
  constexpr float kEndV = 1.67f;

  setDac(dacChannel, kStartV);
  delay(1000);  // let the coil current settle

  float v[kCalPoints];
  float i[kCalPoints];
  for (int p = 0; p < kCalPoints; p++) {
    v[p] = kStartV + (kEndV - kStartV) * p / (float)(kCalPoints - 1);
    setDac(dacChannel, v[p]);
    heartbeatWiggle();
    i[p] = 0.0f;
    for (int a = 0; a < kCalAvg; a++) i[p] += readCurrentMonitor();
    i[p] /= (float)kCalAvg;
  }

  float yIntercept;
  linearFit(v, i, 0, kCalPoints, slope, yIntercept);
  interceptV = -yIntercept / slope;  // DAC volts at exactly 0 A

  if (interceptV <= 1.6f || interceptV >= 1.7f) {
    protocol::fault("magnet DAC channel %d calibration failed: 0 A at %.4f V is implausible",
                    dacChannel, (double)interceptV);
    return false;
  }
  protocol::logf("magnet DAC channel %d: slope %.4f A/V, 0 A at %.4f V", dacChannel,
                 (double)slope, (double)interceptV);
  return true;
}

}  // namespace

void setup() {
  if (!gMagnetAttached) {
    protocol::fault("magnet board not attached");
    return;
  }
  // Claim the control lines (io::configure left them high-impedance).
  pinMode(kEnablePin, OUTPUT);
  digitalWriteFast(kEnablePin, LOW);
  pinMode(kSelectPin, OUTPUT);
  digitalWriteFast(kSelectPin, LOW);  // NC

  if (chips::probe(chips::kMagAdcAddr) != 0) {
    protocol::fault("no magnet ADC at 0x%02X; check wiring", chips::kMagAdcAddr);
    return;
  }
  if (chips::probe(chips::kMagDacAddr) != 0) {
    protocol::fault("no magnet DAC at 0x%02X; check wiring", chips::kMagDacAddr);
    return;
  }
  chips::ad5669Setup(chips::kMagDacAddr, true);
  chipsReady = true;
}

void invalidateSetup() { chipsReady = false; }

void enable(bool on) {
  if (!requireReady()) return;
  digitalWriteFast(kEnablePin, on ? HIGH : LOW);
}

void selectOutput(bool nc) {
  if (!requireReady()) return;
  digitalWriteFast(kSelectPin, nc ? LOW : HIGH);
}

void calibrate() {
  if (!requireReady()) return;
  magnetCalibrated = false;
  enable(true);

  // Zero-current reference with the relay open (coil disconnected).
  switchRelay(false);
  delay(100);
  zeroCurrentV = 0.0f;
  for (int a = 0; a < kCalAvg; a++) {
    zeroCurrentV += readAdcVolts(kAdcCurrentCh);
    delay(10);
  }
  zeroCurrentV /= (float)kCalAvg;
  protocol::logf("magnet zero-current reference: %.4f V", (double)zeroCurrentV);

  switchRelay(true);
  delay(100);

  selectOutput(true);  // NC
  const bool ncOk = calibrateOutput(kDacNcCh, slopeNc, interceptNc);
  enable(false);
  if (!ncOk) return;

  delay(100);
  enable(true);
  selectOutput(false);  // NO
  const bool noOk = calibrateOutput(kDacNoCh, slopeNo, interceptNo);
  enable(false);
  if (!noOk) return;

  // Park both outputs at their zero-current setpoints; otherwise the next
  // enable() would drive the last sweep current through the coil.
  setDac(kDacNcCh, interceptNc);
  setDac(kDacNoCh, interceptNo);
  selectOutput(true);
  magnetCalibrated = true;
}

void calibrateHall(int hallId) {
  if (!requireReady()) return;
  if (!magnetCalibrated) {
    protocol::fault("run calibrateMagnet before calibrateHall");
    return;
  }
  hallCalibrated = false;

  constexpr float kStartA = -0.2f;
  constexpr float kEndA = 0.2f;

  selectOutput(true);
  enable(true);
  setCurrent(true, kStartA);
  delay(1000);

  float amps[kCalPoints];
  float mT[kCalPoints];
  for (int p = 0; p < kCalPoints; p++) {
    amps[p] = kStartA + (kEndA - kStartA) * p / (float)(kCalPoints - 1);
    setCurrent(true, amps[p]);
    heartbeatWiggle();
    mT[p] = 0.0f;
    for (int a = 0; a < kCalAvg; a++) mT[p] += readHallMilliTesla(hallId);
    mT[p] /= (float)kCalAvg;
  }
  setCurrent(true, 0.0f);  // park at zero before disabling
  enable(false);

  linearFit(amps, mT, 0, kCalPoints, hallSlope, hallIntercept);
  hallCalibrated = true;
  protocol::logf("hall calibration: %.4f mT/A, offset %.4f mT", (double)hallSlope,
                 (double)hallIntercept);
}

void setCurrent(bool nc, float amps) {
  if (!requireReady()) return;
  if (!magnetCalibrated) {
    protocol::fault("magnet is not calibrated; run calibrateMagnet");
    return;
  }
  const float volts = nc ? (amps / slopeNc + interceptNc) : (amps / slopeNo + interceptNo);
  setDac(nc ? kDacNcCh : kDacNoCh, volts);
}

void setField(bool nc, float milliTesla) {
  if (!hallCalibrated) {
    protocol::fault("hall sensor is not calibrated; run calibrateHall");
    return;
  }
  setCurrent(nc, (milliTesla - hallIntercept) / hallSlope);
}

float readHallMilliTesla(int hallId) {
  if (hallId < 0 || hallId > 2) {
    protocol::fault("hall sensor id %d out of range 0..2", hallId);
    return 0.0f;
  }
  const float v = readAdcVolts(hallId + 3);
  // Undo the 1.5x divider and the zero-field offset, then 6 mV/G, G -> mT.
  return (v - kHallZeroFieldV) * 1.5f * 1000.0f / 6.0f / 10.0f;
}

float readAdcVolts(int channel) { return io::readAdcVolts(channel, 2); }

void setDacVolts(int channel, float volts) {
  if (!requireReady()) return;
  if (channel < 0 || channel > 8) {
    protocol::fault("magnet DAC channel %d out of range 0..8", channel);
    return;
  }
  setDac(channel, volts);
}

}  // namespace magnet
