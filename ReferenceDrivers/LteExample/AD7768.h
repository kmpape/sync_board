#ifndef _AD7768_H_
#define _AD7768_H_
#include <Arduino.h>
#include <Wire.h>  //includes wire library//
#include "DMAChannel.h"

#define AD7768_CHANNEL_STANDBY_REG      0x00  
#define AD7768_CHANNEL_A_CONFIG_REG     0x01
#define AD7768_CHANNEL_B_CONFIG_REG     0x02
#define AD7768_CHANNEL_MODE_SELECT_REG  0x03
#define AD7768_POWER_MODE_REG           0x04
#define AD7768_GENERAL_CONFIG_REG       0x05
#define AD7768_DATA_CONTROL_REG         0x06
#define AD7768_INTERFACE_CONFIG_REG     0x07
#define AD7768_BIST_RUN_REG             0x08
#define AD7768_DEVICE_STATUS_REG        0x09 // Read only
#define AD7768_GPIO_CONTROL_REG         0x0E
#define AD7768_GPIO_WRITE_REG           0x0F
#define AD7768_GPIO_READ_REG            0x10 // Read only
#define AD7768_PRECHARGE_BUFFER_1_REG   0x11
#define AD7768_PRECHARGE_BUFFER_2_REG   0x12
#define AD7768_POSITIVE_REF_BUFFER_REG  0x13
#define AD7768_NEGATIVE_REF_BUFFER_REG  0x14

#define AD7768_STARTUP_TIME_AFTER_RESET_MS 2.0
// Define the size of the DMA buffer
// #define DMABUFFER_SIZE 4
struct AD7768Data {
  bool error_flag;
  bool filter_not_settled; // true after a reset, power-up or sync 
  bool repeated_data;
  bool filter_type;
  bool filter_saturated;
  uint8_t channel_ID;
  int32_t value; // 24 bit signed integer, we do the sign extension in the getADCVoltages function
  float voltage; // this should not be calculated within this driver file as it is circuit dependent
};

// Declare the DMA buffer
// extern volatile uint64_t dmaBuffer[DMABUFFER_SIZE];

class AD7768 {


 public:
   enum class ChannelMode : uint8_t {
    MODE_A = 0,   // Mode A: fast response, SINC5 filter, ADC will state FilterType = 1 on header bits
    MODE_B = 1    // Mode B: low noise, wideband filter,  ADC will state FilterType = 0 on header bits
  };

  AD7768();
  AD7768(uint8_t chip_select);
  uint32_t* getADCBits(); // returns an array of 32bit integers (header and data) one for each of the 8 channels
  AD7768Data* getADCData();
  uint32_t getADCInt(uint8_t channel_ID); // gets the integer value of the ADC for a specific channel only from the DMA Buffer
  void SetChannelMode(int channel, ChannelMode mode = ChannelMode::MODE_A); // channel is 0-7, mode is 0 or 1, if channel is -1 then set all channels to the mode
  void SetGPIOMode(uint8_t state = 0x00);
  void Initialise(volatile uint64_t* dmaBufferPtrIN);
  void StartADC();
  volatile uint32_t *clkPort;
  volatile uint32_t *dataPort;
  uint32_t clkMask;
  uint32_t dataMask;
  void readStatusReg();

 private:
  // channelModes is reset to all MODE_A in AD7768::Initialise function
  ChannelMode channelModes[8] = {ChannelMode::MODE_A, ChannelMode::MODE_A, ChannelMode::MODE_A, ChannelMode::MODE_A, ChannelMode::MODE_A, ChannelMode::MODE_A, ChannelMode::MODE_A, ChannelMode::MODE_A}; //Initialise all as mode A on startup
  // Declare the DMA channel
  // DMAChannel dmaChannel;
  // void setupFlexIOInput();
  void writeRegister(uint8_t regAddress, uint8_t value);
  uint8_t readRegister(uint8_t regAddress);

  void softReset();
  void syncSPI();

  uint8_t chip_select_pin;
  volatile uint64_t* dmaBufferPtr;


};



#endif