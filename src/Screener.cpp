#include "Screener.h"

#include "Chips.h"
#include "Io.h"
#include "Protocol.h"
#include "State.h"
#include "digitalWriteFast.h"

namespace screener {
namespace {

// Teensy pin 14 = GPIO31 (d31). With J1-22 in its expansion position it
// reaches the header without a level shifter; Iso-2 and Buf1 on the
// screener board turn it into the 5 V TTL control on RFS1.
constexpr int kRfSwitchPin = 14;

// PCA9685 channels, 1-based (channel n is output LED(n-1)).
constexpr int kPwmRfEnable = 1;
constexpr int kPwmRfSpareEnable = 2;
constexpr int kPwmFirstSwitch = 3;      // SWEN1..SWEN7 on channels 3..9
constexpr int kPwmFluxgateEnable = 11;  // channel 10 is unconnected
constexpr int kPwmSensorsEnable = 12;
constexpr int kPwmRtdPolarity = 13;     // Sw1: which RTD drive wire gets the IDAC
constexpr int kPwmMuxA0 = 14;           // MX1..MX3 -> ADG707 A0..A2 on 14..16
constexpr int kNumPwm = 16;

// ADS124S08 registers. INPMUX..IDACMUX are written as one block per
// conversion, so no configuration state lingers between reads.
constexpr uint8_t kRegStatus = 0x01;
constexpr uint8_t kRegInpmux = 0x02;
constexpr uint8_t kRegRef = 0x05;
constexpr uint8_t kRegIdacMag = 0x06;
constexpr uint8_t kRegSys = 0x09;
constexpr uint8_t kStatusPor = 0x80;
constexpr uint8_t kDataRate = 0x34;    // single-shot, low-latency filter, 20 SPS
constexpr uint8_t kSysDefault = 0x10;  // reset value: monitors off
// Internal reference always on (the IDAC needs it even when another
// reference is selected); REFSEL goes in bits 3:2.
constexpr uint8_t kRefBase = 0x02;
// REFP_BUF and REFN_BUF (active low). The buffers serve the external
// references and are on for REF0 and REF1; the datasheet says to disable
// them when the internal reference is selected (as the reference driver does).
constexpr uint8_t kRefBuffersOff = 0x30;
// DRDY is not wired, so each conversion waits this long before reading. A
// 20 SPS single-shot conversion takes a little over one 50 ms period.
constexpr uint32_t kConversionWaitMs = 80;
// RTD conversions use the settings of the LTE senseboard driver, which reads
// the same ADC and RTD front end (ReferenceDrivers/LteExample). kDataRate
// plus global chop: each result is the mean of two conversions with the
// PGA inputs swapped, cancelling the ADC's own offset and its drift.
constexpr uint8_t kDataRateRtd = 0xB4;
// PGA DELAY = 4096 tMOD (16 ms) after START and after each chop swap, for
// the input filters to settle after the excitation polarity flips.
constexpr uint8_t kPgaDelayRtd = 0xC0;
// 113 ms for a 20 SPS global-chop conversion (datasheet table 18) plus the
// two 16 ms delays is ~145 ms; the senseboard waits 180 ms.
constexpr uint32_t kRtdConversionWaitMs = 180;
constexpr float kFullScaleCode = 8388608.0f;  // 2^23
constexpr int32_t kCodeMax = 0x7FFFFF;
constexpr int32_t kCodeMin = -0x800000;

// RTD front end (RTD+ADC notes on the ScreenerMobo sheet): the IDAC on
// AIN10 (RTDI) drives the RTD through Sw1 and the drive mux, and the current
// returns through the 4.7 kOhm reference resistor whose buffered ends are
// REFP0/REFN0. The sense mux brings the RTD's own voltage to AIN0/AIN1.
constexpr int kRtdAinP = 0;
constexpr int kRtdAinN = 1;
constexpr int kRtdIdacPin = 10;
constexpr uint8_t kRtdIdac500uA = 0x05;
constexpr int kRtdGain = 2;
constexpr float kRtdRefOhms = 4700.0f;  // R11-1, 0.01 %
constexpr uint32_t kRtdSettleMs = 10;   // input RC filters + IDAC turn-on
// Conversions per chopped reading, alternating polarity (as the senseboard
// does); even, so both polarities weigh equally.
constexpr int kRtdChopReadings = 6;

constexpr float kRfAdcRefV = 1.8f;      // ADS7828 REF = AD8302 #1 VREF (via R5-1)
constexpr float kInternalRefV = 2.5f;

// The REF register for a reference selection.
uint8_t refRegister(Ref ref) {
  return (uint8_t)(kRefBase | ((uint8_t)ref << 2) | (ref == Ref::kInternal ? kRefBuffersOff : 0));
}

bool chipsReady = false;
// Which chips answered at setup. Each is optional (setup warns when one is
// missing) and every function needs only its own, so a partly populated or
// partly working board can still be exercised piece by piece.
bool pwmPresent = false;
bool rfAdcPresent = false;
bool adcPresent = false;
float pwmDuty[kNumPwm + 1] = {0.0f};  // as last written, 1-based
// millis() when each output last went fully on, for PoweredRail's settle.
uint32_t pwmOnSinceMs[kNumPwm + 1] = {0};

bool requireReady() {
  if (!gScreenerAttached) {
    protocol::fault("screener board not attached");
    return false;
  }
  if (!chipsReady) {
    protocol::fault("screener board not set up; run setupScreener after enabling the system");
    return false;
  }
  return true;
}

}  // namespace

bool requirePwm() {
  if (!requireReady()) return false;
  if (!pwmPresent) {
    protocol::fault("the screener's PCA9685 (0x%02X) did not answer at setup; is the board "
                    "powered (12 V on H2)?", chips::kScreenerPwmAddr);
    return false;
  }
  return true;
}

bool requireAdc() {
  if (!requireReady()) return false;
  if (!adcPresent) {
    protocol::fault("the screener's ADS124S08 did not answer at setup (not fitted, or no SPI link)");
    return false;
  }
  return true;
}

namespace {

bool requireRfAdc() {
  if (!requireReady()) return false;
  if (!rfAdcPresent) {
    protocol::fault("the screener's ADS7828 (0x%02X) did not answer at setup",
                    chips::kScreenerAdcAddr);
    return false;
  }
  return true;
}

bool validDuty(float duty) {
  if (duty < 0.0f || duty > 1.0f) {
    protocol::fault("duty %.3f out of range 0..1", (double)duty);
    return false;
  }
  return true;
}

void writePwm(int channel, float duty) {
  chips::pca9685SetDuty(chips::kScreenerPwmAddr, channel, duty);
  const int first = channel == 0 ? 1 : channel;
  const int last = channel == 0 ? kNumPwm : channel;
  for (int i = first; i <= last; i++) {
    if (duty >= 1.0f && pwmDuty[i] < 1.0f) pwmOnSinceMs[i] = millis();
    pwmDuty[i] = duty;
  }
}

void writeLevel(int channel, bool high) { writePwm(channel, high ? 1.0f : 0.0f); }

int enableChannel(Enable which) {
  switch (which) {
    case Enable::kRf: return kPwmRfEnable;
    case Enable::kRfSpare: return kPwmRfSpareEnable;
    case Enable::kFluxgate: return kPwmFluxgateEnable;
    case Enable::kSensors: return kPwmSensorsEnable;
  }
  return kPwmSensorsEnable;  // unreachable: every Enable is listed
}

// ADG707 pair: A2..A0 = channel - 1 selects S<channel>A/B.
void selectRtd(int channel, bool polarityHigh) {
  const int address = channel - 1;
  for (int bit = 0; bit < 3; bit++) writeLevel(kPwmMuxA0 + bit, (address >> bit) & 1);
  writeLevel(kPwmRtdPolarity, polarityHigh);
}

// Resets the ADS124S08 and checks it answers; false if it does not.
bool setupAdc() {
  chips::ads124s08Reset();
  // A pattern written and read back proves both SPI directions through the
  // isolator (a dead MISO reads all 0s or all 1s).
  for (const uint8_t pattern : {(uint8_t)0x5A, (uint8_t)0xA5}) {
    chips::ads124s08WriteRegs(kRegInpmux, &pattern, 1);
    const uint8_t readBack = chips::ads124s08ReadReg(kRegInpmux);
    if (readBack != pattern) {
      protocol::logf("WARNING: no screener ADS124S08 answering on CS1 (wrote 0x%02X, read "
                     "0x%02X); RTD, Hall, fluxgate and humidity readings unavailable",
                     pattern, readBack);
      return false;
    }
  }
  // Clear the power-on flag so a later spontaneous reset can be spotted.
  const uint8_t clearStatus = 0x00;
  chips::ads124s08WriteRegs(kRegStatus, &clearStatus, 1);
  // Internal reference on now, so it has settled (1 uF on REFOUT) before
  // the first conversion.
  const uint8_t ref = refRegister(Ref::kInternal);
  chips::ads124s08WriteRegs(kRegRef, &ref, 1);
  return true;
}

// Waits ms with the heartbeat kept going: the main loop stops toggling it
// while a command blocks, and a chopped RTD reading blocks over a second.
void wait(uint32_t ms) {
  const uint32_t start = millis();
  while (millis() - start + 20 <= ms) io::heartbeatPulse(10);
  const uint32_t spent = millis() - start;
  if (spent < ms) delay(ms - spent);
}

// Sets up the conversions that follow. rtd selects the RTD settings (global
// chop, 16 ms delay) and turns IDAC1 on at 500 uA into AIN10, left on until
// excitationOff. Faults and returns false if the ADC has reset since setup
// (it would have lost the reference and filter settings).
bool configure(int ainp, int ainn, Ref ref, int gain, bool rtd) {
  int gainBits = -1;
  for (int b = 0; b < 8; b++) {
    if ((1 << b) == gain) gainBits = b;
  }
  if (gainBits < 0) {
    protocol::fault("ADC gain %d not one of 1, 2, 4, ... 128", gain);
    return false;
  }
  if ((chips::ads124s08ReadReg(kRegStatus) & kStatusPor) != 0) {
    adcPresent = false;
    protocol::fault("the screener ADC has reset since setup (power glitch?); run setupScreener");
    return false;
  }
  const uint8_t regs[6] = {
      (uint8_t)((ainp << 4) | ainn),                               // INPMUX
      (uint8_t)((rtd ? kPgaDelayRtd : 0x00) |                      // PGA (bypassed at
                (gain == 1 ? 0x00 : (0x08 | gainBits))),           //   gain 1)
      rtd ? kDataRateRtd : kDataRate,                              // DATARATE
      refRegister(ref),                                            // REF
      rtd ? kRtdIdac500uA : (uint8_t)0x00,                         // IDACMAG
      (uint8_t)(rtd ? (0xF0 | kRtdIdacPin) : 0xFF),                // IDACMUX: IDAC1 only
  };
  chips::ads124s08WriteRegs(kRegInpmux, regs, 6);
  return true;
}

void excitationOff() {
  const uint8_t idacOff[2] = {0x00, 0xFF};  // IDACMAG, IDACMUX
  chips::ads124s08WriteRegs(kRegIdacMag, idacOff, 2);
}

// One single-shot conversion with the settings from configure.
int32_t convert(uint32_t waitMs) {
  chips::ads124s08Start();
  wait(waitMs);
  return chips::ads124s08ReadData();
}

// count RTD conversions on one channel, the excitation polarity starting at
// firstHigh and alternating; the mean resistance, or 0 after a fault. The
// excitation stays on (and the mux on the channel) for the whole sequence.
float rtdOhms(int channel, int count, bool firstHigh) {
  if (!requireAdc() || !requirePwm()) return 0.0f;  // the PCA9685 drives the RTD muxes
  if (channel < 1 || channel > kNumRtds) {
    protocol::fault("RTD channel %d out of range 1..%d", channel, kNumRtds);
    return 0.0f;
  }
  const PoweredRail muxes(Enable::kSensors, kSensorsSettleMs);
  if (!muxes.ok()) return 0.0f;
  selectRtd(channel, firstHigh);
  if (!configure(kRtdAinP, kRtdAinN, Ref::kRtd, kRtdGain, true)) return 0.0f;
  wait(kRtdSettleMs);
  float sum = 0.0f;
  for (int i = 0; i < count; i++) {
    const bool polarityHigh = (i % 2 == 0) == firstHigh;
    // Only the polarity changes from here on; the PGA delay after START
    // covers the settling.
    if (i > 0) writeLevel(kPwmRtdPolarity, polarityHigh);
    const int32_t code = convert(kRtdConversionWaitMs);
    if (code >= kCodeMax || code <= kCodeMin) {
      excitationOff();
      protocol::fault("RTD channel %d over range (open, or not connected?)", channel);
      return 0.0f;
    }
    // Sw1 (TMUX1136) connects SxA to Dx with its select high, which puts the
    // IDAC on the D+ wire and makes the sense pair read positive. Low puts it
    // on D- and the sign flips.
    const float ratio = code / (kFullScaleCode * kRtdGain);
    sum += polarityHigh ? ratio : -ratio;
  }
  excitationOff();
  return sum / count * kRtdRefOhms;
}

}  // namespace

void setup() {
  invalidateSetup();
  if (!gScreenerAttached) {
    protocol::fault("screener board not attached");
    return;
  }
  // Claim the RF switch line (io::configure left it high-impedance).
  pinMode(kRfSwitchPin, OUTPUT);
  digitalWriteFast(kRfSwitchPin, LOW);

  pwmPresent = chips::probe(chips::kScreenerPwmAddr) == 0;
  if (pwmPresent) {
    // io::configure already initialised the PWM chip at enable, but only if
    // the board was powered by then.
    chips::pca9685Init(chips::kScreenerPwmAddr, /*slowClock=*/true);
    writePwm(0, 0.0f);
  } else {
    protocol::logf("WARNING: no screener PCA9685 at 0x%02X (is the board powered, 12 V on "
                   "H2?); switches and enables unavailable", chips::kScreenerPwmAddr);
  }

  rfAdcPresent = chips::probe(chips::kScreenerAdcAddr) == 0;
  if (!rfAdcPresent) {
    protocol::logf("WARNING: no screener ADS7828 at 0x%02X; RF readings unavailable",
                   chips::kScreenerAdcAddr);
  }

  // The ADS124S08's analog supply is the board's 5 V rail, which only comes
  // up with the heartbeat; let it settle before resetting the chip.
  wait(100);
  adcPresent = setupAdc();
  if (adcPresent) wait(60);
  chipsReady = true;
}

void invalidateSetup() {
  chipsReady = false;
  pwmPresent = false;
  rfAdcPresent = false;
  adcPresent = false;
  for (int i = 1; i <= kNumPwm; i++) pwmDuty[i] = 0.0f;  // io::configure zeroed the chip
}

bool pwmFound() { return pwmPresent; }
bool rfAdcFound() { return rfAdcPresent; }
bool adcFound() { return adcPresent; }

void setSwitch(int channel, float duty) {
  if (!requirePwm()) return;
  if (channel < 0 || channel > kNumSwitches) {
    protocol::fault("screener switch %d out of range 0..%d", channel, kNumSwitches);
    return;
  }
  if (!validDuty(duty)) return;
  if (channel == 0) {
    if (duty > 0.0f) {
      protocol::fault("turning all switches on at once is not allowed");
      return;
    }
    for (int i = 0; i < kNumSwitches; i++) writePwm(kPwmFirstSwitch + i, 0.0f);
    return;
  }
  writePwm(kPwmFirstSwitch + channel - 1, duty);
}

void setEnable(Enable which, bool on) {
  if (!requirePwm()) return;
  writeLevel(enableChannel(which), on);
}

PoweredRail::PoweredRail(Enable rail, uint32_t settleMs) {
  if (!requirePwm()) return;
  channel_ = enableChannel(rail);
  previousDuty_ = pwmDuty[channel_];
  if (previousDuty_ < 1.0f) writeLevel(channel_, true);
  const uint32_t onForMs = millis() - pwmOnSinceMs[channel_];
  if (onForMs < settleMs) wait(settleMs - onForMs);
  ok_ = true;
}

PoweredRail::~PoweredRail() {
  if (ok_ && previousDuty_ < 1.0f) writePwm(channel_, previousDuty_);
}

void setPwm(int channel, float duty) {
  if (!requirePwm()) return;
  if (channel < 0 || channel > kNumPwm) {
    protocol::fault("screener PWM channel %d out of range 0..%d", channel, kNumPwm);
    return;
  }
  if (!validDuty(duty)) return;
  if (channel == 0 && duty > 0.0f) {
    protocol::fault("turning all PWM outputs on at once is not allowed");
    return;
  }
  writePwm(channel, duty);
}

void setRfSwitch(bool on) {
  if (!requireReady()) return;
  digitalWriteFast(kRfSwitchPin, on ? HIGH : LOW);
}

float readRfVolts(int channel) {
  if (!requireRfAdc()) return 0.0f;
  if (channel < 1 || channel > 8) {
    protocol::fault("RF ADC channel %d out of range 1..8", channel);
    return 0.0f;
  }
  return chips::ads7828Read(chips::kScreenerAdcAddr, channel - 1) * kRfAdcRefV / 4096.0f;
}

float readAdcRatio(int ainp, int ainn, Ref ref, int gain, int32_t& code) {
  code = 0;
  if (!requireAdc()) return 0.0f;
  if (ainp < 0 || ainp > kAinCom || ainn < 0 || ainn > kAinCom) {
    protocol::fault("ADC inputs %d/%d out of range 0..%d (%d = AINCOM)", ainp, ainn, kAinCom,
                    kAinCom);
    return 0.0f;
  }
  if (!configure(ainp, ainn, ref, gain, false)) return 0.0f;
  code = convert(kConversionWaitMs);
  return code / (kFullScaleCode * gain);
}

bool readSensorRatio(int ainp, int ainn, Ref ref, const char* what, float& ratio) {
  int32_t code;
  ratio = readAdcRatio(ainp, ainn, ref, 1, code);
  if (protocol::faultPending()) return false;
  if (code >= kCodeMax || code <= kCodeMin) {
    protocol::fault("%s over range (AIN%d - AIN%d)", what, ainp, ainn);
    return false;
  }
  return true;
}

float readRtdOhms(int channel) { return rtdOhms(channel, kRtdChopReadings, true); }

float readRtdOhms(int channel, bool polarityHigh) { return rtdOhms(channel, 1, polarityHigh); }

float readSupplyVolts(bool analog) {
  if (!requireAdc()) return 0.0f;
  // SYS_MON 011 = (AVDD - AVSS) / 4, 100 = DVDD / 4; the chip measures them
  // at gain 1, here against the internal reference.
  const uint8_t monitor = (uint8_t)(((analog ? 0x3 : 0x4) << 5) | kSysDefault);
  chips::ads124s08WriteRegs(kRegSys, &monitor, 1);
  const bool ok = configure(0, kAinCom, Ref::kInternal, 1, false);
  const int32_t code = ok ? convert(kConversionWaitMs) : 0;
  const uint8_t restore = kSysDefault;
  chips::ads124s08WriteRegs(kRegSys, &restore, 1);
  if (!ok) return 0.0f;
  return 4.0f * kInternalRefV * code / kFullScaleCode;
}

}  // namespace screener
