#ifndef _HDC322_H_
#define _HDC3022_H_

#include <Arduino.h>
#include <Wire.h>
#include "Teensy_HID.h"

class HDC3022{
    public:

        struct HDC3022Data{
            float temperature;
            float humidity;
        };

        HDC3022();
        // speficy I2C bus addressing for our purpose//
        HDC3022(uint8_t addr);  // intialize using address
        void Initialise();      //initialisation function

        HDC3022Data readSingleShot();
        HDC3022Data readContinuous();

    private:
        uint8_t i2caddr;
        uint8_t crc8_nrsc5_gen(uint16_t); // CRC8 generator function
        float last_temperature = -100.0;
        float last_humidity = -1.0;
};

// Sets the device to just provide a measurement on the trigger
#define SINGLE_LOW_NOISE_MODE_MSB 0x24
#define SINGLE_LOW_NOISE_MODE_LSB 0x00
#define SINGLE_LOW_POWER_MODE_MSB 0x24
#define SINGLE_LOW_POWER_MODE_LSB 0xFF

// Generates a measurement for each once every two seconds over the i2c bus
#define AUTO2SEC_LOW_NOISE_MODE_MSB 0x20
#define AUTO2SEC_LOW_NOISE_MODE_LSB 0x32
#define AUTO2SEC_LOW_POWER_MODE_MSB 0x20
#define AUTO2SEC_LOW_POWER_MODE_LSB 0xFF

#define MEASUREMENT_READOUT_MSB 0xE0
#define MEASUREMENT_READOUT_LSB 0x00

#define SOFT_RESET_MSB 0x30
#define SOFT_RESET_LSB 0xA2


#endif