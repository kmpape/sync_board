#pragma once //means this is only included once in the compilation process
#include "GenerationConfig.h"

///////////////////////////
// Teensy 4.1 Pinout
///////////////////////////

// To understand more look at SensorBoard.SchDoc in schematics.

const int IR_SYNC_PIN_AL = 0;
const int IR_SYNC_PIN_AR = 1; 
const int IR_SYNC_PIN_BL = 2;

#if GENERATION == V1_MACHINES
    const int Actuators_Enable = 3; //Enable pin for the actuator board. Goes to the actuator board.
#elif GENERATION == V2_MACHINES || GENERATION == V3_MACHINES
    const int MISC_IO = 3; // goes to ethernet port - set this as input or output in Setup() dependening on what we need
#endif

const int IR_SYNC_PIN_BR = 4;
const int IR_SYNC_PIN_CL = 5;
const int IR_SYNC_PIN_CR = 6;
const int IR_SYNC_PIN_DL = 7;

const int ADC_DCLK_T = 8; // Not used, set in setupFlexIOInput

const int ADC_RDY_S = 9; // Not used, set in setupFlexIOInput

const int Senseboard_ADC2_CS = 10; //Chip select for the ADS124S08 on SenseBoard (CS2 on schematic)

const int SPI_MOSI0  = 11; //SPI MOSI. MOSI0 on schematic.
const int SPI_MISO0  = 12; //SPI MISO. MISO0 on schematic.
const int SPI_SCK0 = 13; //SPI SCK. Also the indicator LED on the Teensy4.1 so will flash with signals. SCK0 on schematic.

const int STIR_STEP_PIN_CR = 14;
const int STIR_STEP_PIN_DR = 15;

const int I2C_SCL = 16; //I2C SCL. SCL on schematic. 
const int I2C_SDA = 17; //I2C SDA. SDA on schematic. nNote you have to use Wire1 everywhere due to it being on the second I2C bus.

const int STIR_STEP_PIN_DL = 18;
const int STIR_STEP_PIN_CL = 19;

const int Topboard_ADC_CS = 20; //Chip select for the AD7768 on Topboard (CS0 on schematic)

const int Load_Cell_Chop = 21; //Chop pin for the load cell ADC. Goes to the load cell ADC. Chop1 net on the schematic

const int IR_SYNC_PIN_DR = 22;

const int STIR_STEP_PIN_AL = 23;
const int STIR_STEP_PIN_AR = 24;

const int Accel_Interrupt = 25; // Interrupt when accelerometer exceed predefined thresholds on FXLS8964

#if GENERATION == V1_MACHINES
    const int Other_Interrupt = 26; // Combined other and valve interrupt 
#elif GENERATION == V2_MACHINES || GENERATION == V3_MACHINES
    // Powers 3.3V and 5V analogue rail on Senseboard
    // Powers 3.3V and 5V rail on Heatboard
    // Powers 3V and 5V rail on Topboard
    const int D_Power_En = 26; 
#endif

const int Senseboard_ADC_CS = 27; //Chip select for the AD7768 on SenseBoard (CS1 on schematic)

const int STIR_STEP_PIN_BL = 28;

#if GENERATION == V1_MACHINES
    const int Topboard_and_Heatboard_PowerEnable = 31;
#elif GENERATION == V2_MACHINES || GENERATION == V3_MACHINES
    const int I2C_MUX_Reset = 29; // Active Low, goes to the reset pin on I2C multiplexer on topboard that reads mixing temps
    const int SRX = 30; // Unused at the moment, goes to our fake ethernet port
    const int STX = 31; // Unused at the moment, goes to our fake ethernet port
#endif

const int ADC_DOUT_T = 32; // Not used, set in setupFlexIOInput

#if GENERATION == V2_MACHINES || GENERATION == V3_MACHINES
    // Heartbeat
    const int Heartbeat = 33; //Heartbeat pin. Goes to Expansion 1+2 headers + LED + Magnet Drive.
#endif

const int ADC_DCLK_S = 34; // Not used, set in setupFlexIOInput
const int ADC_DOUT_S = 35; // Not used, set in setupFlexIOInput

const int STIR_STEP_PIN_BR = 36;

const int ADC_RDY_T = 37; // Not used, set in setupFlexIOInput

const int I2C_Reset = 38; // I2C Reset pin, active low

#if GENERATION == V1_MACHINES   
    const int Senseboard_PowerEnable = 39;
    const int Heartbeat = 40; //Heartbeat pin. Goes to Expansion 1+2 headers + LED + Magnet Drive.
#elif GENERATION == V2_MACHINES || GENERATION == V3_MACHINES
    // Powers IMB heaters
    // Powers heatboard LEDs, stir coils, heaters, valves, fans, and UV
    const int P_Power_En = 39;
    const int Other_Interrupt = 40;
#endif

const int ADC_Reset = 41; // Resets ADC chips
