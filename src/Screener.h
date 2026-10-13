#pragma once
#include <Arduino.h>

// Screener board driver (hardware/ScreenerMobo), plugged into the SyncBoard's
// Expansion 2 header (H4; H3 is wired the same for everything used here).
// The board runs from its own 12 V supply on H2 and sits behind isolators:
//
//   PCA9685 @0x51       power-switch inputs, rail enables, RTD mux + polarity
//   ADS7828 @0x4B       RF detector outputs (4x AD8318 power, 2x AD8302
//                       gain/phase), 1.8 V full scale
//   ADS124S08 on CS1    RTDs, and the sensor boards on Con_S (FieldSensor.h:
//                       fluxgate, Hall sensor; AirSensor.h: humidity sensor)
//   GPIO31              RF switch control: Teensy pin 14, unshifted, via
//                       SyncBoard jumper J1-22 in its expansion position
//
// The isolated heartbeat (ENWD) enables the power switches and the 5 V
// analog and RF rails, and its inverse drives the PCA9685 output enable, so
// all of it drops to off when the SyncBoard stops.

namespace screener {

constexpr int kNumSwitches = 7;
constexpr int kNumRtds = 8;

// Rail and detector enables; numeric values are part of the wire protocol.
enum class Enable : uint8_t {
  kRf = 0,        // AD8318 detectors on RF inputs 1-3 (RF_Enable)
  kRfSpare = 1,   // AD8318 detector on the spare RF input 4 (RF_Enable2)
  kFluxgate = 2,  // 3V3_Fluxgate regulator: the FieldSensor's DRV425
  kSensors = 3,   // 5V_Sensors + 5V_Messy regulators (Hall and humidity
                  // sensors, LED bar displays) and the RTD multiplexers' enable
};

// ADS124S08 reference; numeric values are part of the wire protocol.
enum class Ref : uint8_t {
  kRtd = 0,       // REFP0/REFN0: voltage across the 4.7 kOhm RTD reference
  kHall = 1,      // REFP1/REFN1 (= AIN6/AIN7): conditioned Hall excitation
  kInternal = 2,  // 2.5 V internal reference
};

// ADS124S08 input code for AINCOM (0..11 are AIN0..AIN11).
constexpr int kAinCom = 12;

// After system enable: claims the RF switch line and probes the three chips
// independently, turning every PCA9685 output off and resetting the
// ADS124S08. A missing chip is logged as a warning, not a fault: every
// function below needs only its own chip and faults if that one is missing.
// Must be re-run after every system enable.
void setup();

// Which chips answered at the last setup.
bool pwmFound();    // PCA9685: switches, enables, RTD mux + polarity
bool rfAdcFound();  // ADS7828: RF detectors
bool adcFound();    // ADS124S08: RTDs, Hall, fluxgate, humidity

// Fault (with the reason) and return false unless the board is set up and
// that chip answered, so a reading can check before powering anything.
bool requirePwm();
bool requireAdc();

// Called by System on disable (the control line and chips are torn down).
void invalidateSetup();

// Power switch 1..7, duty 0..1 (PWM at ~200 Hz). Low-side: the load sits
// between 12 V and VL on the channel's HP-1 header. Channel 0 turns all
// seven off (off only).
void setSwitch(int channel, float duty);

void setEnable(Enable which, bool on);

// Raw PCA9685 access, channel 1..16 or 0 = all (off only); bring-up use.
void setPwm(int channel, float duty);

// Powers a rail for the length of one reading. The fluxgate and sensors
// rails heat the sensor head (hardware/FieldSensor, AirSensor), so readings
// switch them on only while they need them: the constructor switches the
// rail on if it is off and waits until it has been on for settleMs, and the
// destructor puts it back as it was. A rail held on with setEnable stays on,
// and only waits out what is left of settleMs. ok() is false after a fault
// (board not set up, or no PCA9685), and the rail is left alone.
class PoweredRail {
 public:
  PoweredRail(Enable rail, uint32_t settleMs);
  ~PoweredRail();
  PoweredRail(const PoweredRail&) = delete;
  PoweredRail& operator=(const PoweredRail&) = delete;
  bool ok() const { return ok_; }

 private:
  int channel_ = 0;
  float previousDuty_ = 0.0f;
  bool ok_ = false;
};

// Longest settle a reading accepts, in ms.
constexpr uint32_t kMaxSettleMs = 5000;
// Settle after switching the sensors rail on for an RTD reading: the muxes
// enable at once, and the 5V_Sensors LDO (ADP7118, SS open) starts in 380 us.
constexpr uint32_t kSensorsSettleMs = 20;

// RF switch control (GPIO31 -> Iso-2 -> Buf1 -> 5 V TTL on RFS1).
void setRfSwitch(bool on);

// RF detector output in volts, ADS7828 channel 1..8.
float readRfVolts(int channel);

// One ADS124S08 conversion of AIN<ainp> - AIN<ainn> at gain 1..128 (gain 1
// bypasses the PGA). 20 SPS single-shot: blocks ~80 ms. Returns
// Vin / Vref, and the raw signed 24-bit code in code.
float readAdcRatio(int ainp, int ainn, Ref ref, int gain, int32_t& code);

// readAdcRatio at gain 1, for the sensor boards: false after a fault, which
// names what was being read if the result is over range.
bool readSensorRatio(int ainp, int ainn, Ref ref, const char* what, float& ratio);

// Four-wire RTD resistance on mux channel 1..8 (1-6: HRTD headers, 7: the
// FieldSensor, 8: the AirSensor), measured ratiometrically against the
// 4.7 kOhm reference, with the ADC in global chop at 20 SPS. The mean of six
// conversions with the 500 uA excitation reversed between each (as the LTE
// senseboard reads its RTDs), which cancels thermal-EMF / rectified-RF
// offsets in the wiring. The muxes' enable is the sensors rail, which the
// reading powers if it is off (PoweredRail). Blocks ~1.1 s.
float readRtdOhms(int channel);

// One conversion at one excitation polarity, for bring-up: polarityHigh
// drives the RTD_Polarity line high, sending the excitation into the D+ wire
// instead of D-. The result is sign-corrected so both polarities read the
// same positive resistance; half their difference is the wiring offset.
// Blocks ~190 ms.
float readRtdOhms(int channel, bool polarityHigh);

// The ADS124S08's supply monitors: analog = AVDD (the board's
// heartbeat-gated 5 V analog rail), otherwise DVDD (always-on 3.3 V).
float readSupplyVolts(bool analog);

}  // namespace screener
