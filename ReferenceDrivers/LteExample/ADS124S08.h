#ifndef _ADS124S08_H_
#define _ADS124S08_H_
#include <Arduino.h>
#include <Wire.h>  //includes wire library//

#define ADS124S08_CMD_WAKEUP        0x02
#define ADS124S08_CMD_SLEEP         0x04
#define ADS124S08_CMD_RESET         0x06
#define ADS124S08_CMD_START         0x08
#define ADS124S08_CMD_STOP          0x0A
#define ADS124S08_CMD_RDATA         0x12

#define ADS124S08_STATUS_REG        0x01
#define ADS124S08_MUX_REG           0x02
#define ADS124S08_GAIN_REG          0x03
#define ADS124S08_DATARATE_REG      0x04
#define ADS124S08_REF_REG           0x05
#define ADS124S08_IDAC1_REG         0x06 // excitation current register 1
#define ADS124S08_IDAC_MUX_REG      0x07 // excitation current register 2 

#define ADS124S08_RESET_DELAY_MS    10  // Time to wait after reset command, 4096/fclk, Timing Characteristics pg 14

enum class ADS124S08_MODE : const uint8_t {
  RTD = 1,
  LED_CURRENT = 2,
  PRESSURE_1 = 3,
  PRESSURE_2 = 4,
  GAIN_CAL_1 = 5,
  GAIN_CAL_2 = 6,
  GAIN_CAL_3 = 7,
  OFFSET_CAL = 8
};


/*
tclk = 1 / fclk = 1 / 4.096 Mhz = 0.244 us
tmod = 16 * tclk = 3.906 us
delay after write reg = 14 * 3.906 us = 54.68 us

all use low latency filter

data rates:
rtd:          20SPS
pressure reg: 50SPS
led current:  50SPS

global chop enabled:
rtd:          global chop enabled
pressure reg: global chop disabled
led current:  global chop disabled

conversion time for single shot:
rtd:          113.008 ms + 54.68 us = 113.06268 ms (table 18)
pressure reg:  20.156 ms + 54.68 us =  20.21068 ms (table 13)
led current:   20.156 ms + 54.68 us =  20.21068 ms (table 13)

*/


class ADS124S08 {
 private:
  static constexpr int maxCalibrationStep = 5; //How many calibration steps are there in total?
  static constexpr int maxRTDReadings = 6; // How many times do we measure each RTD. Ideally should be an even number.

  float RTDReadingSum = 0.0; // Stores RTD running sum as we acquire new values.
  float RTDReadingFinal = 0.0; // Stores the previous RTD value calculated.

  float gainCalibration1 = 0; // Stores gain calibration first value
  float gainCalibration2 = 0;
  float gainCalibration3 = 0;
  float gainCalibrationFactor = 1.0; //Defaults at 1

  int32_t readADCRaw(); // Raw read command returns signed integere

  void writeRegister1Byte(uint8_t regAddress, uint8_t value);
  uint8_t readRegister1Byte(uint8_t regAddress);
  void sendCommand(uint8_t command);

 public:
  ADS124S08();
  void Initialise();
  void readData();
  void debugBuffer();
  int calibrate(int calibrationStep); 
  float readADC(); 
  float Configure();
  void startConversion();
  void configure(ADS124S08_MODE mode);
  void setMultiplexer(ADS124S08_MODE mode);
  float getCalibrationFactor();
  
  byte getStatus();


  int readRTD(int RTDIndex, int RTDStep);
  float getLastRTD();
};



#endif