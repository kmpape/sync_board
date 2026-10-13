#include <Arduino.h>

////Address book. Naming is Board_Device_ADR


// PCA9505 addresses all start with 0100 and then have three configurable bits to allow up to 8 on a bus
//  0100 A2 A1 A0
#define Heatboard_IO_1_ADR 0x23  // 0b0100011 - IO chip to supply reactors 1-4 and associated valves
#define Heatboard_IO_2_ADR 0x24  // 0b0100100 - IO chip to supply reactors 5-8 and associated valves
#define Heatboard_IO_3_ADR 0x25  // 0b0100101 - IO chip to supply valve chips INN_10-INN_19
#define Heatboard_IO_4_ADR 0x26  // 0b0100110 - IO chip for misc left over components
#define Senseboard_IO_1_ADR 0x20 // 0b0100000 - IO chip for OD configuration
#define Topboard_IO_1_ADR 0x21   // 0b0100001 - IO chip for indicator LEDS
#define Topboard_IO_2_ADR 0x22   // 0b0100010 - IO chip for indicator LEDS

// PCA9685 addresses all start with 1 and then have six configurable bits
//  1 A5 A4 A3 A2 A1 A0
#define Heatboard_PWM_1_ADR 0x42 // 0b1000010 - PWM to heaters and external IO
#define Heatboard_PWM_2_ADR 0x43 // 0b1000011 - PWM for Reactor UV LEDS
#define Heatboard_PWM_3_ADR 0x47 // 0b1000111 - PWM for Reactor Visible RGB LEDs
#define Topboard_PWM_1_ADR 0x40  // 0b1000000 - PWM for generating sync signals for the strain gauges
#define Topboard_PWM_2_ADR 0x41  // 0b1000001 - PWM for the IMB heaters

// ADS122C04
#define Heatboard_ADC_ADR 0x45 // 0b1000101 - ADC to read current sensing

// AD5673 has five MSBs as 00011 and two configurable bits
//  00011 A1 A0
#define Heatboard_DAC_ADR 0x0D // 0b0001101 - DAC to generate analog signals for the stirring controllers, sets maximum current through the coils and microstepping level

// AD5675 has five MSBs as 00011 and two configurable bits
//  00011 A1 A0
#define Senseboard_DAC_ADR 0x0C // 0b0001100 - DAC to generate analog voltages for the OD measurements

#define Senseboard_Accelerometer_ADR 0x18 // 0b0011000
#define Senseboard_Thermo_ADR 0x48        // 0b1001000
#define Senseboard_Humidity_ADR 0x44      // 0b1000100

#define Topboard_Thermo_ADR 0x49 // 0b1001001 - Thermo chip for the topboard

#define Topboard_Thermo_MUX_ADR 0x70 // 0b1110000 - Multiplexer to address the individual i2c communications with each IMB IR thermoneter and also SMD board thermometers

#define Teensy_FRAM 0x7C // 0b1111100 Inbuilt opart on the teensy that we dont really need to care about. But dont add a chip that crashes with this address!

#define IR_temp_chip_ADR 0x5A // 0b1011010 - I2C address of the MLX90614 IR thermometer 
