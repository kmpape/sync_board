#pragma once
#include <Arduino.h>

// Screener board driver (hardware/ScreenerMobo), plugged into the SyncBoard's
// Expansion 2 header (H4; H3 is wired the same for everything used here).
// The board runs from its own 12 V supply on H2 and sits behind isolators:
//
//   PCA9685 @0x51       power-switch inputs, rail enables, RTD mux + polarity
//   ADS7828 @0x4B       RF detector outputs (4x AD8318 power, 2x AD8302
//                       gain/phase), 1.8 V full scale
//   ADS124S08 on CS1    RTDs, Hall sensor, fluxgate, humidity sensor
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
  kFluxgate = 2,  // 3V3_Fluxgate regulator: fluxgate supply on the sensor flex
  kSensors = 3,   // 5V_Sensors + 5V_Messy regulators (flex sensors, LED bar
                  // displays) and the RTD multiplexers' enable
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

// Called by System on disable (the control line and chips are torn down).
void invalidateSetup();

// Power switch 1..7, duty 0..1 (PWM at ~200 Hz). Low-side: the load sits
// between 12 V and VL on the channel's HP-1 header. Channel 0 turns all
// seven off (off only).
void setSwitch(int channel, float duty);

void setEnable(Enable which, bool on);

// Raw PCA9685 access, channel 1..16 or 0 = all (off only); bring-up use.
void setPwm(int channel, float duty);

// RF switch control (GPIO31 -> Iso-2 -> Buf1 -> 5 V TTL on RFS1).
void setRfSwitch(bool on);

// RF detector output in volts, ADS7828 channel 1..8.
float readRfVolts(int channel);

// One ADS124S08 conversion of AIN<ainp> - AIN<ainn> at gain 1..128 (gain 1
// bypasses the PGA). 20 SPS single-shot: blocks ~80 ms. Returns
// Vin / Vref, and the raw signed 24-bit code in code.
float readAdcRatio(int ainp, int ainn, Ref ref, int gain, int32_t& code);

// Four-wire RTD resistance on mux channel 1..8 (1-6: HRTD headers, 7-8: the
// sensor flex), measured ratiometrically against the 4.7 kOhm reference.
// polarityHigh drives the RTD_Polarity line high, sending the 500 uA
// excitation into the D+ wire instead of D-; the result is sign-corrected so
// both polarities read the same positive resistance, and their mean cancels
// thermal-EMF / rectified-RF offsets. Needs the sensors enable on. Blocks
// ~90 ms.
float readRtdOhms(int channel, bool polarityHigh);

// The ADS124S08's supply monitors: analog = AVDD (the board's
// heartbeat-gated 5 V analog rail), otherwise DVDD (always-on 3.3 V).
float readSupplyVolts(bool analog);

}  // namespace screener
