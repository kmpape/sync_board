#pragma once
#include <Arduino.h>

// AirSensor board (hardware/AirSensor): sits in the airflow, and carries the
// connector for the cable to the screener board's Con_S (the FieldSensor
// flex plugs into it). The screener's ADS124S08 reads it:
//
//   SHT40I-HD1F (RH1)  analog RH and T outputs, 10-90 % of its supply (the
//                      flex 5 V, 5V_Sensors: sensors enable). BConditioner
//                      halves each about 2V5_Offset: RH on AIN9, T on AIN8.
//   PT1000 (RTD-1)     screener RTD channel 8 (screener::readRtdOhms).
//
// Like the FieldSensor's readings, this one powers the sensors rail only
// while it runs (screener::PoweredRail). The Python client
// (syncboard.board.AirSensorApi) turns the results into %RH and deg C.

namespace air_sensor {

constexpr int kRtdChannel = 8;
// The SHT40I's first measurement comes 7-15 ms after power-up, and its
// analog outputs then take 200 ms to settle (datasheet, table 4).
constexpr uint32_t kHumiditySettleMs = 300;

// What the SHT40I outputs are read against; numeric values are part of the
// wire protocol.
enum class Reference : uint8_t {
  // Ratiometric to the flex 5 V as the FieldSensor's Hall sensor senses it
  // (REF1). The SHT40I's transfer function is ratiometric, so this cancels
  // the supply, and 2V5_Offset cancels too. Needs the FieldSensor.
  kFlexSupply = 0,
  // Against 2V5_Offset and the internal reference. Works without the
  // FieldSensor; the host assumes the nominal 5 V supply and 2.5 V offset.
  kOffset = 1,
};

// The SHT40I's RH and T outputs. kFlexSupply: fractions of the sensor's
// supply (AIN9 - AIN7 and AIN8 - AIN7 against REF1, after checking the
// supply sense). kOffset: output minus 2V5_Offset in volts at the sensor
// (twice AIN9 - AIN5 and AIN8 - AIN5, against the internal reference).
void readOutputs(Reference ref, uint32_t settleMs, float& rh, float& t);

}  // namespace air_sensor
