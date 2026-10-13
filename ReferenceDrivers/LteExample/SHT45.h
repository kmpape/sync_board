#ifndef _SHT45_H_
#define _SHT45_H_

#include <Arduino.h>
#include <Wire.h>
#include "Teensy_HID.h"

class SHT45{
    public:

        struct SHT45Data{
            float temperature;
            float humidity;
        };

        SHT45();
        // speficy I2C bus addressing for our purpose//
        SHT45(uint8_t addr);  // intialize using address
        void Initialise();      //initialisation function

        SHT45Data readContinuous();

    private:
        uint8_t i2caddr;
        uint8_t crc8_nrsc5_gen(uint16_t); // CRC8 generator function
        float last_temperature = -100.0;
        float last_humidity = -1.0;
};

// Sets the device to just provide a measurement on the trigger
#define SHT45_SINGLE_HIGH_PRECISION_MODE 0xFD
#define SHT45_SINGLE_MEDIUM_PRECISION_MODE 0xF6
#define SHT45_SINGLE_LOW_PRECISION_MODE 0xE0

#define SHT45_SOFT_RESET 0x94


#endif