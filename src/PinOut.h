#pragma once //means this is only included once in the compilation process

// Teensy 4.1 Pinout
// To understand more look at SyncBoard1.SchDoc in schematics.


// Heartbeat
const int Heartbeat = 41; //Heartbeat pin. Goes to Expansion 1+2 headers + LED + Magnet Drive.


// Fixed digital IO pins.
const int D_IN_1 = 1; //Digital input 1 - d0 on schematic
const int D_IN_2 = 2; //Digital input 2 - d1 on schematic
const int D_IN_3 = 3; //Digital input 3 - d2 on schematic
const int D_IN_4 = 4; //Digital input 4 - d3 on schematic

const int D_OUT_1 = 5; //Digital output 1 - d4 on schematic
const int D_OUT_2 = 6; //Digital output 2 - d5 on schematic
const int D_OUT_3 = 7; //Digital output 3 - d6 on schematic
const int D_OUT_4 = 8; //Digital output 4 - d7 on schematic


// GPIO pins
const int GPIO_13 = 30; //GPIO 13 - d13 on schematic. Can be level shifted to 3.3V or 5V. Can be jumped to Camera_Spare on J1-1.
const int GPIO_25 = 22; //GPIO 25 - d25 on schematic. Can be level shifted to 3.3V or 5V. 
const int GPIO_26 = 23; //GPIO 26 - d26 on schematic. Can be level shifted to 3.3V or 5V. 
const int GPIO_27 = 9; //GPIO 27 - d27 on schematic. Can be level shifted to 3.3V or 5V. 
const int GPIO_28 = 24; //GPIO 28 - d28 on schematic. Can be level shifted to 3.3V or 5V. 
const int GPIO_29 = 16; //GPIO 29 - d29 on schematic. Can also be used as Analog Input. Also goes to Expansion 1+2+Magnet headers. Choice is on J1-20
const int GPIO_30 = 17; //GPIO 30 - d30 on schematic. Can also be used as Analog Input. Also goes to Expansion 1+2+Magnet headers. Choice is on J1-21
const int GPIO_31 = 14; // GPIO 31 - d31 on schematic. Can be level shifted to 3.3V or 5V. Also can be used as a PWM output. Also goes to Expansion 1+2 headers. Choice is on J1-22
const int GPIO_32 = 15; // GPIO 32 - d32 on schematic. Can be level shifted to 3.3V or 5V. Also can be used as a PWM output. Also goes to Expansion 1+2 headers. Choice is on J1-23

// SPI pins
const int SPI_CS0 = 10; //SPI CS0 - which goes to the high speed DAC. CS0 on schematic.
const int SPI_MOSI0 = 11; //SPI MOSI. MOSI0 on schematic.
const int SPI_MISO0 = 12; //SPI MISO. MISO0 on schematic.
const int SPI_SCK0 = 13; //SPI SCK. Also the indicator LED on the Teensy4.1 so will flash with signals. SCK0 on schematic.
const int SPI_CS1 = 0; //SPI CS1 - which goes to Expansion 1+2 headers, as well as the Display Counter if jumped. CS1 on schematic.

//I2C Pins
const int I2C_SDA = 18; //I2C SDA. SDA0 on schematic. Goes to chips on this board + expansion/LED boards.
const int I2C_SCL = 19; //I2C SCL. SCL0 on schematic. Goes to chips on this board + expansion/LED boards.


//DAC Pins
const int DAC_LDAC_Bar = 21; //Active low LDAC pin for the DAC. Is DD1 on schematic.
const int DAC_CLR_Bar = 20; //Active low CLR pin for the DAC. Is DD2 on schematic.

//Camera Pins
const int Camera_Trigger_ready = 25; //Camera's ready output. Signal from camera saying it is ready to accept next trigger, Tr0 on Kinetix and D8 on schematic.
const int Camera_Trigger_In = 32; //Camera's trigger input. The signal we send to to the camera to trigger it. TrI on Kinetix and D15 on schematic.
const int Camera_Reading = 28; //Signal from camera saying it is reading out data. RO on Kinetix and D11 on schematic.
const int Camera_LED1 = 31; //Signal from camera saying activate LED1. D14 on schematic.
const int Camera_LED2 = 29; //Signal from camera saying activate LED2. D12 on schematic.
const int Camera_LED3 = 27; //Signal from camera saying activate LED3. D10 on schematic.
const int Camera_LED4 = 26; //Signal from camera saying activate LED4. D9 on schematic.
//const int Camera_Spare = 30; //Spare camera pin. DSpare on schematic, jumped via J1-1 to D13. This doesnt get used at present.

//LED Driver Pins
const int LED_1_Enable = 33; //Enable signal for LED1. D16 on schematic.
const int LED_2_Enable = 34; //Enable signal for LED2. D17 on schematic.
const int LED_3_Enable = 35; //Enable signal for LED3. D18 on schematic.
const int LED_4_Enable = 36; //Enable signal for LED4. D19 on schematic.
const int LED_5_Enable = 37; //Enable signal for LED5. D20 on schematic.
const int LED_6_Enable = 38; //Enable signal for LED6. D21 on schematic.
const int LED_7_Enable = 39; //Enable signal for LED7. D22 on schematic.
const int LED_8_Enable = 40; //Enable signal for LED8. D23 on schematic.
