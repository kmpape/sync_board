#pragma once
#include <Arduino.h>

// FieldSensor flex (hardware/FieldSensor): the sensor head on the axis
// between the resonator and the magnet face. It plugs into the AirSensor
// board, whose cable carries its lines to the screener board's Con_S, and
// the screener's ADS124S08 reads it:
//
//   DRV425 fluxgate (B1)  VOUT on BOut1 -> FluxgateP (AIN2), REFOUT (VCC/2,
//                         RSEL1 high) on BOut2 -> FluxgateN (AIN3). 15 Ohm
//                         shunt: 0.732 V/mT, +-2 mT. Supply 3V3_Fluxgate
//                         (fluxgate enable).
//   DRV5055Z3 Hall (H1)   OUT on HallS, its supply sensed at the sensor on
//                         HallP/HallN. BConditioner halves each about
//                         2V5_Offset: AIN4, and AIN6/AIN7 (= REF1). Supply
//                         5V_Sensors (sensors enable).
//   PT1000 (RTD-1)        screener RTD channel 7 (screener::readRtdOhms).
//
// The DRV425 draws 6 mA plus up to ~30 mA of compensation current and heats
// the head, so each reading powers its rail only while it runs
// (screener::PoweredRail): on, wait settleMs, convert, off. A rail held on
// with setScreenerEnable stays on. Results are volts or ratios; the Python
// client (syncboard.board.FieldSensorApi) turns them into mT.

namespace field_sensor {

constexpr int kRtdChannel = 7;
constexpr int kMaxCount = 16;  // conversions per reading, ~80 ms each
// Default settle after switching a rail on. The fluxgate rail's ADM7150
// rises (10-90 %) in about 1.2 ms per uF on BYP, and BYP has 122 uF (C6-18
// 22 uF plus CE3-8 100 uF): ~150 ms. Measured (2026-09-30), REFOUT reaches
// 1.5 V at ~0.2 s, and VOUT - REFOUT is within ~1 mV (1.4 uT) of its final
// value by ~0.5 s. At 100 ms REFOUT was still ~1.3 V.
constexpr uint32_t kFluxgateSettleMs = 500;
// The ADP7118 on 5V_Sensors (SS open) starts in 380 us, the DRV5055 in well
// under a ms.
constexpr uint32_t kHallSettleMs = 20;

// Fluxgate output VOUT - REFOUT in volts into volts[0..count-1]: count
// conversions of AIN2 - AIN3 against the internal 2.5 V reference, after
// checking that REFOUT sits near 1.65 V (a DRV425 that is unpowered or not
// connected would otherwise read as zero field).
void readFluxgateVolts(int count, uint32_t settleMs, float* volts);

// Hall sensor output as a fraction of its own supply, (OUT - GND) /
// (VCC - GND) as sensed at the sensor, into ratios[0..count-1]: 0.5 at zero
// field. AIN4 - AIN7 against REF1, so 2V5_Offset cancels.
void readHallRatio(int count, uint32_t settleMs, float* ratios);

// The Hall sensor's supply VCC - GND at the sensor, in volts: the flex 5 V
// after the cable. AIN6 - AIN5 and AIN5 - AIN7 against the internal
// reference, so 2V5_Offset cancels.
float readSupplyVolts(uint32_t settleMs);

// Faults unless the Hall supply sense (HallP) reads at least 4.5 V: REF1 is
// made of it, so readings against REF1 check it first. For callers that
// hold the sensors rail on (the AirSensor's ratiometric humidity reading).
bool requireSupply();

}  // namespace field_sensor
