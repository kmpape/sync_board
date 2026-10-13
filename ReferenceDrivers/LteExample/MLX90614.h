#ifndef _MLX90614_H_
#define _MLX90614_H_
#include <Arduino.h>
#include <Wire.h>  //includes wire library//
#include "Teensy_HID.h"
#include "I2CAddressBook.h"


#define CRC_ERROR 7 //CRC error code polynomial = x8+x2+x1+1

//Register Adresses for reading from RAM//
//we have a single but I am not sure whether the addresses are correct, just going to use source 1//

#define IR1         0x04 //Raw temp source 1
#define IR2         0x05 //Raw temp source 2 for dual zone sensor
#define T_AMB       0x06 //Ambient temp (linearised)
#define T_OBJ1      0x07 //source1 temp (linearised)
#define T_OBJ2      0x08 //source2 temp (linearised) for dual zone sensor 

//EEPROM adresses for storage of settings//

#define OBJ_MAX     0x00 //To set the temperature object range max
#define OBJ_MIN     0x01 //To set the temperature object range min
#define PWM_CTRL    0x02 //To set the PWM output control register
#define AMB_RANGE   0x03 //To set the ambient temperature range
#define EMISS       0x04 //To set the emissivity value
#define CONFIG      0x05 // configuration register
#define ADDR        0x0E //SMBus address 

//Error flags//
#define RFLAF_CMD   0xf0 //R/w flag register command sensor responds with flag resgister and PEC for error handling
#define EE_BUSY     0x80 //EE busy flag
#define EE_DEAD     0x20 //EE double error flag
#define EE_INIT     0x10 //EE still initialising flag

class MLX90614 {

public:
    MLX90614(uint8_t addr);
    void Initialise();
    uint8_t getIIRCoeff();
    double getEmissivity();
    void writeEmissivity(double emissivity);
    uint8_t calculateCrc(const uint8_t *data, uint8_t len);
    double getObjectTempC(); 
    double getAmbientTempC();

private:
    uint8_t i2caddr;
    double rawToCelsius(uint16_t rawValue);
    uint16_t read16(uint8_t reg);
};

#endif
