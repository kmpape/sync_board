#include "FieldSensor.h"

#include "Protocol.h"
#include "Screener.h"

namespace field_sensor {
namespace {

using screener::Enable;
using screener::PoweredRail;
using screener::Ref;

// ADS124S08 inputs. The Hall lines come through BConditioner, which halves
// each about 2V5_Offset: AIN4 = (HallS + Voff) / 2, AIN6/AIN7 = (HallP/N +
// Voff) / 2.
constexpr int kAinFluxgateP = 2;  // BOut1 = DRV425 VOUT
constexpr int kAinFluxgateN = 3;  // BOut2 = DRV425 REFOUT
constexpr int kAinHall = 4;
constexpr int kAinOffset = 5;      // 2V5_Offset (through R12-10)
constexpr int kAinSupplyP = 6;     // REFP1
constexpr int kAinSupplyN = 7;     // REFN1
constexpr float kInternalRefV = 2.5f;
constexpr float kOffsetV = 2.5f;   // nominal 2V5_Offset, for the supply check only
// REFOUT is VCC/2 in the DRV425's ratiometric mode: 1.65 V on 3V3_Fluxgate.
constexpr float kRefOutMinV = 1.4f;
constexpr float kRefOutMaxV = 1.9f;
// The DRV5055 (and the AirSensor's SHT40I, on the same 5 V) need 4.5 V.
constexpr float kMinSupplyV = 4.5f;

bool validCount(int count) {
  if (count < 1 || count > kMaxCount) {
    protocol::fault("conversion count %d out of range 1..%d", count, kMaxCount);
    return false;
  }
  return true;
}

}  // namespace

bool requireSupply() {
  float ratio;
  if (!screener::readSensorRatio(kAinSupplyP, kAinOffset, Ref::kInternal, "Hall supply sense",
                                 ratio)) {
    return false;
  }
  const float hallP = 2.0f * ratio * kInternalRefV + kOffsetV;
  if (hallP < kMinSupplyV) {
    protocol::fault("the FieldSensor's supply sense (HallP) reads %.2f V, below 4.5 V: is the "
                    "flex plugged into the AirSensor, and the AirSensor cable into Con_S?",
                    (double)hallP);
    return false;
  }
  return true;
}

void readFluxgateVolts(int count, uint32_t settleMs, float* volts) {
  if (!validCount(count) || !screener::requireAdc()) return;
  const PoweredRail power(Enable::kFluxgate, settleMs);
  if (!power.ok()) return;
  float ratio;
  if (!screener::readSensorRatio(kAinFluxgateN, screener::kAinCom, Ref::kInternal,
                                 "fluxgate REFOUT", ratio)) {
    return;
  }
  const float refOut = ratio * kInternalRefV;
  if (refOut < kRefOutMinV || refOut > kRefOutMaxV) {
    protocol::fault("the fluxgate's REFOUT (FluxgateN) reads %.3f V, not ~1.65 V: is the "
                    "FieldSensor connected and 3V3_Fluxgate up (LL-3)? A longer settle may "
                    "help", (double)refOut);
    return;
  }
  for (int i = 0; i < count; i++) {
    if (!screener::readSensorRatio(kAinFluxgateP, kAinFluxgateN, Ref::kInternal, "fluxgate",
                                   ratio)) {
      return;
    }
    volts[i] = ratio * kInternalRefV;
  }
}

void readHallRatio(int count, uint32_t settleMs, float* ratios) {
  if (!validCount(count) || !screener::requireAdc()) return;
  const PoweredRail power(Enable::kSensors, settleMs);
  if (!power.ok() || !requireSupply()) return;
  for (int i = 0; i < count; i++) {
    // (AIN4 - AIN7) / (AIN6 - AIN7) = (HallS - HallN) / (HallP - HallN)
    if (!screener::readSensorRatio(kAinHall, kAinSupplyN, Ref::kHall, "Hall sensor",
                                   ratios[i])) {
      return;
    }
  }
}

float readSupplyVolts(uint32_t settleMs) {
  if (!screener::requireAdc()) return 0.0f;
  const PoweredRail power(Enable::kSensors, settleMs);
  if (!power.ok()) return 0.0f;
  float high, low;  // (HallP - Voff) / 2 and (Voff - HallN) / 2
  if (!screener::readSensorRatio(kAinSupplyP, kAinOffset, Ref::kInternal, "Hall supply sense",
                                 high) ||
      !screener::readSensorRatio(kAinOffset, kAinSupplyN, Ref::kInternal, "Hall supply sense",
                                 low)) {
    return 0.0f;
  }
  return 2.0f * (high + low) * kInternalRefV;
}

}  // namespace field_sensor
