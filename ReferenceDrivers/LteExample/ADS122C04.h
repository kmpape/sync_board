#ifndef _ADS122C04_H_
#define _ADS122C04_H_
#include <Arduino.h>
#include <Wire.h>  //includes wire library// 
#include "Teensy_HID.h"

#include "I2CAddressBook.h" //includes the I2C address book for the different boards//

//based on the driver here https://github.com/sparkfun/SparkFun_ADS122C04_ADC_Arduino_Library/blob/main/src/SparkFun_ADS122C04_ADC_Arduino_Library.cpp with many modifications and simplifications for our purpose
//We are only using this for voltage conversions and not using the internal IDACS therefore the IDAC config is not included. 

/* do we need a timeout for conversion? */
// Single Conversion Timeout (millis)
// The maximum time we will wait for DRDY to go valid for a single conversion
#define CONVERSION_TIMEOUT 75


#define TEMPERATURE_MODE   0x3

//Read or write to registers rr= register address 00 to 11
//0010 or 0100 rrxx     

#define WRITE_CMD(reg)     (0x20 | (reg << 2))    //0x20 to write Shift is 2-bit in ADS122C04
#define READ_CMD(reg)      (0x40 | (reg << 2))    //0x40 as the command to read Shift is 2-bit in ADS122C04

// Commands to write to ADS122C04 (Table 16 in Datasheet)
#define RESET          0x06     //0000 011x      Reset
#define START          0x08     //0000 100x      Start/Sync
#define POWERDOWN      0x02     //0000 001x      PowerDown
#define RDATA          0x10     //0001 xxxx      RDATA
#define RREG           0x20     //0010 xxxx      Read Register
#define WREG           0x40     //0100 xxxx      Write Register
#define MUX_AIN0_POS   0x0000 // we are only using the AI0 and AI1 pins so this is enough for the multiplexer 
#define MUX_AIN0_NEG   0b0011 // we are only using the AI0 and AI1 pins so this is enough for the multiplexer 


// the different register addresses
#define CONFIG_0_REG        0 // Configuration Register 0
#define CONFIG_1_REG        1 // Configuration Register 1
#define CONFIG_2_REG        2 // Configuration Register 2
#define CONFIG_3_REG        3 // Configuration Register 3

// Gain Configuration
#define GAIN_1            0x0
#define GAIN_2            0x1 //2
#define GAIN_4            0x2 //4
#define GAIN_8            0x3 //8
#define GAIN_16           0x4 //16
#define GAIN_32           0x5 //32
#define GAIN_64           0x6 //64
#define GAIN_128          0x7 //128

// PGA Bypass (PGA is disabled when the PGA_BYPASS bit is set, can only be disable when gain is 1,2 or 4 )
#define PGA_DISABLED      0x1
#define PGA_ENABLED       0x0

//DATA RATE in SPS
#define DATA_RATE_20SPS   0x0
#define DATA_RATE_45SPS   0x1
#define DATA_RATE_90SPS   0x2
#define DATA_RATE_175SPS  0x3
#define DATA_RATE_330SPS  0x4
#define DATA_RATE_600SPS  0x5
#define DATA_RATE_1000SPS 0x6

//data rate settings - normal or turbo mode
#define MODE_NORMAL    0x0
#define MODE_TURBO     0x1



// Conversion Mode - we probably want single shot mode
#define SINGLE_SHOT   0x0 //only converts when queried
#define CONTINUOUS    0x1 //always converting

#define VREF_INTERNAL            0x0 //2.048V internal
#define VREFEXT_REF_PINS        0x1 //REFp and REFn external
#define VREF_AVDD                0x2 //Analog Supply AVDD and AVSS
// Temperature Sensor Mode
#define TEMP_SENSOR_OFF          0x0
#define TEMP_SENSOR_ON           0x1

// Data Counter 
#define COUNTER_DISABLE             0x0
#define COUNTER_ENABLE              0x1 

#define ADS122C04_CONVERSION_TIME 60 //in ms for 20SPS

// Union for the 14-bit internal Temperature
// To simplify converting from uint16_t to int16_t
// without using a cast
union internal_temperature_union{
  int16_t INT16;
  uint16_t UINT16;
};


// Union for the 24-bit raw voltage
// To simplify converting from uint32_t to int32_t
// without using a cast
union raw_voltage_union{
  int32_t INT32;
  uint32_t UINT32;
};

class ADS122C04 {
  public:
    ADS122C04();
    //address
    ADS122C04(uint8_t addr); 

    void Initialise(); 
    bool checkDataReady(); // Check if the data is ready to be read
    
    void startConversion(void); // Start a conversion
    // Read the raw signed 24-bit ADC value as int32_t
    // This uses the internal 2.048V reference with the gain set to 1
    // The LSB is 2.048 / 2^23 = 0.24414 uV (0.24414 microvolts)
    float readRawVoltage(uint8_t rate = DATA_RATE_20SPS);
    // Read the internal temperature (C)
    float readInternalTemperature(uint8_t rate = DATA_RATE_20SPS);

    bool reset(void); // Reset the ADS122C04
    bool start(void); // Start a conversion
    bool powerdown(void); // Put the chip into low power mode

    bool setInputMultiplexer(uint8_t mux_config); // Configure the input multiplexer
    bool setGain(uint8_t gain_config); // Configure the gain
    bool enablePGA(uint8_t enable); // Enable/disable the Programmable Gain Amplifier
    bool setDataRate(uint8_t rate); // Set the data rate (sample speed)
    bool setOperatingMode(uint8_t mode ); // Configure the operating mode (normal / turbo)
    bool setConversionMode(uint8_t mode ); // Configure the conversion mode (single-shot / continuous)
    bool setVoltageReference(uint8_t ref ); // Configure the voltage reference
    bool enableInternalTempSensor(uint8_t enable ); // Enable / disable the internal temperature sensor
    bool setDataCounter(uint8_t enable); // Enable / disable the conversion data counter

    
    uint8_t getGain(void); // Get the gain setting
    uint8_t getPGAstatus(void); // Get the Programmable Gain Amplifier status
    uint8_t getDataRate(void); // Get the data rate (sample speed)
    uint8_t getOperatingMode(void); // Get the operating mode (normal / turbo)
    uint8_t getConversionMode(void); // Get the conversion mode (single-shot / continuous) configuration
    uint8_t getInternalTempSensorStatus(void); // Get the internal temperature sensor status
    uint8_t getDataCounter(void); // Get the data counter status

private: 
  uint8_t i2caddr;
      // Internal temperature sensor resolution
      // One 14-bit LSB equals 0.03125°C
  const float TEMPERATURE_SENSOR_RESOLUTION = 0.03125;

  void writeReg(uint8_t reg, uint8_t writeValue); // write a value to the selected register
  uint8_t readReg(uint8_t reg); // read a value from the selected register (returned in readValue)
  
  uint8_t sendCommand(uint8_t command); // write to the selected command register    
   
};

#endif
