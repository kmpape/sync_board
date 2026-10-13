#ifndef _ChipManager_H_
#define _ChipManager_H_

#include "GlobalVariables.h"
#include "PinOut.h"
#include "AD5673.h"
#include "AD5675.h"
#include "AD7768.h"
#include "ADS122C04.h"
#include "ADS124S08.h"
#include "FXLS8964.h"
#include "PCA9505.h"
#include "PCA9685.h"
#include "TCA9548.h"
#include "TMP117.h"
#include "MLX90614.h"

#if GENERATION == V3_MACHINES
    #include "SHT45.h"
#else 
    #include "HDC3022.h"
#endif

/////////////////////////////////
////// CHIP PIN DEFINITIONS /////
/////////////////////////////////
const uint8_t CURRENT_SENSE_BANK = 3;          // MM0, MM1, and MM2 live on Bank 3 on Heatboard_IO_4
const uint8_t CURRENT_SENSE_OFFSET = 2;        // MM2 is pos 2 of the bank, MM1 is pos 3 of the bank, MM0 is pos 4 of the bank
const uint8_t LIQUID_SENSING_ENABLE_PIN = 44;  // Topboard_IO_2
const uint8_t EXCITE_ENABLE_PIN = 54;          // Senseboard_IO_1
const uint8_t RTD_ENABLE_PIN = 53;             // Senseboard_IO_1
const uint8_t FAN_PIN = 15;                    // Heatboard_PWM_1
const uint8_t PERISTALTIC_PIN = 16;            // Heatboard_PWM_1
const uint8_t MM0_PIN = 41;                    // Heatboard_IO_4
const uint8_t MM1_PIN = 40;                    // Heatboard_IO_4
const uint8_t MM2_PIN = 38;                    // Heatboard_IO_4
const uint8_t VS0_PIN = 36;                    // Heatboard_IO_4
const uint8_t VS1_PIN = 37;                    // Heatboard_IO_4
const uint8_t PREG1_PIN = 7;                   // Senseboard_DAC
const uint8_t PREG2_PIN = 6;                   // Senseboard_DAC
const uint8_t CHOP0_PIN = 9;                   // Topboard_PWM_1
const uint8_t CHOP2_PIN = 12;                  // Topboard_PWM_1
const uint8_t CONF1_PIN = 7;                   // Topboard_PWM_1
const uint8_t CONF2_PIN = 8;                   // Topboard_PWM_1
const uint8_t CONF3_PIN = 10;                  // Topboard_PWM_1
const uint8_t CONF4_PIN = 11;                  // Topboard_PWM_1
const uint8_t VBRIDGE_EN_PIN = 6;              // Topboard_PWM_1
const uint8_t FLUIDSENSE_EN_PIN = 13;          // Topboard_PWM_1
const uint8_t FLUID_SENSOR_ADC_CHANNEL = 6;    // Topboard_ADC channel for fluid sensor
const uint8_t MIXING_HEATER_CURRENT_ADC_CHANNEL = 5; // Topboard_ADC channel for mixing heater current sense
const uint8_t WASTE_LOADCELL_ADC_CHANNEL = 4; // Topboard_ADC channel for waste load cell
const uint8_t VL_PIN = 4;                     // Senseboard_DAC these are all channel numbers as opposed to pin numbers
const uint8_t VM_PIN = 1;                     // Senseboard_DAC
const uint8_t VH_PIN = 0;                     // Senseboard_DAC
const uint8_t VR_PIN = 2;                     // Senseboard_DAC
const uint8_t VCM_PIN = 3;                    // Senseboard_DAC

struct ChipManager {
    PCA9505 Heatboard_IO_1      = PCA9505(Heatboard_IO_1_ADR);  // Reactors 1-4 valves, stirring, cuvette indicator leds, and valve fault
    PCA9505 Heatboard_IO_2      = PCA9505(Heatboard_IO_2_ADR);  // Reactors 5-8 valves, stirring, cuvette indicator leds, and valve fault
    PCA9505 Heatboard_IO_3      = PCA9505(Heatboard_IO_3_ADR);  // other valves
    PCA9505 Heatboard_IO_4      = PCA9505(Heatboard_IO_4_ADR);  // more valves, stir voltage level select, stir decay config, select current channel to read
    PCA9505 Senseboard_IO_1     = PCA9505(Senseboard_IO_1_ADR);
    PCA9505 Topboard_IO_1       = PCA9505(Topboard_IO_1_ADR);
    PCA9505 Topboard_IO_2       = PCA9505(Topboard_IO_2_ADR);

    PCA9685 Heatboard_PWM_1     = PCA9685(Heatboard_PWM_1_ADR); // set fan duty cycle, set reactor heater duty cycle
    PCA9685 Heatboard_PWM_2     = PCA9685(Heatboard_PWM_2_ADR); // sets UV duty cycle
    PCA9685 Heatboard_PWM_3     = PCA9685(Heatboard_PWM_3_ADR); // sets RGB LED
    PCA9685 Topboard_PWM_1      = PCA9685(Topboard_PWM_1_ADR);
    PCA9685 Topboard_PWM_2      = PCA9685(Topboard_PWM_2_ADR);

    AD5673 Heatboard_DAC        = AD5673(Heatboard_DAC_ADR);    // determines stir power, and microstepping level
    AD5675 Senseboard_DAC       = AD5675(Senseboard_DAC_ADR);   // voltages needed for OD measurements and pressure reg levels

    AD7768 Senseboard_ADC       = AD7768(Senseboard_ADC_CS);    // Used to quickly read ODs             // SPI chip so no address 
    AD7768 Topboard_ADC         = AD7768(Topboard_ADC_CS);      // Used to quickly read Load Cells      // SPI chip so no address
    ADS124S08 Senseboard_ADC2   = ADS124S08();                  // Reads RTDs and IR LED current        // SPI chip so no address - could add chip select as a variable but no real need since only one
    ADS122C04 Heatboard_ADC     = ADS122C04(Heatboard_ADC_ADR); // Current sensing

    TMP117 Senseboard_TMP       = TMP117(Senseboard_Thermo_ADR);
    TMP117 Topboard_TMP_1       = TMP117(Topboard_Thermo_ADR);
    TMP117 Topboard_TMP_2       = TMP117(Topboard_Thermo_ADR);
    TMP117 Topboard_TMP_3       = TMP117(Topboard_Thermo_ADR);

    #if GENERATION == V3_MACHINES
        SHT45 Senseboard_HDC      = SHT45(Senseboard_Humidity_ADR);
    #else 
        HDC3022 Senseboard_HDC      = HDC3022(Senseboard_Humidity_ADR);
    #endif
    
    FXLS8964 Senseboard_ACCEL   = FXLS8964(Senseboard_Accelerometer_ADR);

    // if we ever change receiving messages to be in its own thread, we will need to make this 
    // a shared resource that can only be accessed by one message processing at a time
    // since it is used by both the MIXING and the board temperature sensors
    TCA9548 Topboard_Thermo_MUX = TCA9548(Topboard_Thermo_MUX_ADR);
    MLX90614 IR_Temp_Sensor     = MLX90614(IR_temp_chip_ADR); // I2C address is set in the constructor
};

extern ChipManager CHIPS;

#endif
