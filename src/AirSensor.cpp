#include "AirSensor.h"

#include "FieldSensor.h"
#include "Screener.h"

namespace air_sensor {
namespace {

using screener::Enable;
using screener::PoweredRail;
using screener::Ref;

// ADS124S08 inputs; BConditioner halves RH and T about 2V5_Offset.
constexpr int kAinT = 8;
constexpr int kAinRh = 9;
constexpr int kAinOffset = 5;   // 2V5_Offset (through R12-10)
constexpr int kAinSupplyN = 7;  // REFN1: the FieldSensor's GND sense, halved
constexpr float kInternalRefV = 2.5f;

}  // namespace

void readOutputs(Reference ref, uint32_t settleMs, float& rh, float& t) {
  rh = t = 0.0f;
  if (!screener::requireAdc()) return;
  const PoweredRail power(Enable::kSensors, settleMs);
  if (!power.ok()) return;
  if (ref == Reference::kFlexSupply) {
    // (AIN9 - AIN7) / (AIN6 - AIN7) = (RH - HallN) / (HallP - HallN)
    if (!field_sensor::requireSupply() ||
        !screener::readSensorRatio(kAinRh, kAinSupplyN, Ref::kHall, "SHT40I RH output", rh)) {
      return;
    }
    screener::readSensorRatio(kAinT, kAinSupplyN, Ref::kHall, "SHT40I T output", t);
    return;
  }
  float rhRatio, tRatio;
  if (!screener::readSensorRatio(kAinRh, kAinOffset, Ref::kInternal, "SHT40I RH output",
                                 rhRatio) ||
      !screener::readSensorRatio(kAinT, kAinOffset, Ref::kInternal, "SHT40I T output",
                                 tRatio)) {
    return;
  }
  rh = 2.0f * rhRatio * kInternalRefV;
  t = 2.0f * tRatio * kInternalRefV;
}

}  // namespace air_sensor
