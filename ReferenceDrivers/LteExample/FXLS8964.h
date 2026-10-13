#ifndef _FXLS8964_H_
#define _FXLS8964_H_

#include <Arduino.h>
#include <Wire.h>
#include "Teensy_HID.h"

/*
Interrupt setup on pg 51 of datasheet. 
I think the way we set this up is set bit 5 of SENS_CONFIG to 1 
(so it wakes on SDCD outside threshold),. Basically the thing will ideally 
go in wake mode intially (NEVER let it hibernate since without pin 7 you 
cant wake it) and then will go to sleep, waking from sleep by setting some 
SDCD thresholds such that it interrupts when they are crossed. Unclear if 
should just do XY or Z too to account for gravity. 
Ideal would if can do the sum -g
*/

class FXLS8964{
    public:
        struct FXLS8964Data
        {
            float x;
            float y;
            float z;
        };

        FXLS8964();
        // speficy I2C bus addressing for our purpose//
        FXLS8964(uint8_t addr);  // intialize using address
        
        void Initialise();      //initialisation function

        int8_t readTemperature();
        FXLS8964Data readAccel();
        uint8_t clearInterrupt();



    private:
        uint8_t i2caddr;
        float convertToG(int16_t rawData);
        int16_t convertToLSB(float gValue);
        uint8_t FSR = 0b01;

        // Values at which the chip will generate an interrupt when exceeded
        float GLowerThresholdTrigger = -0.1;  // originally -0.5
        float GUpperThresholdTrigger = 0.1;  // originally 0.5


};

enum
{
    REG_FXLS8964_INT_STATUS        = 0x00,
    REG_FXLS8964_TEMP_OUT          = 0x01,
    REG_FXLS8964_VECM_LSB          = 0x02,
    REG_FXLS8964_VECM_MSB          = 0x03,
    REG_FXLS8964_OUT_X_LSB         = 0x04,
    REG_FXLS8964_OUT_X_MSB         = 0x05,
    REG_FXLS8964_OUT_Y_LSB         = 0x06,
    REG_FXLS8964_OUT_Y_MSB         = 0x07,
    REG_FXLS8964_OUT_Z_LSB         = 0x08,
    REG_FXLS8964_OUT_Z_MSB         = 0x09,
    REG_FXLS8964_BUF_STATUS        = 0x0B,
    REG_FXLS8964_BUF_X_LSB         = 0x0C,
    REG_FXLS8964_BUF_X_MSB         = 0x0D,
    REG_FXLS8964_BUF_Y_LSB         = 0x0E,
    REG_FXLS8964_BUF_Y_MSB         = 0x0F,
    REG_FXLS8964_BUF_Z_LSB         = 0x10,
    REG_FXLS8964_BUF_Z_MSB         = 0x11,
    REG_FXLS8964_PROD_REV          = 0x12,
    REG_FXLS8964_WHO_AM_I          = 0x13,
    REG_FXLS8964_SYS_MODE          = 0x14,
    REG_FXLS8964_SENS_CONFIG1      = 0x15,
    REG_FXLS8964_SENS_CONFIG2      = 0x16,
    REG_FXLS8964_SENS_CONFIG3      = 0x17,
    REG_FXLS8964_SENS_CONFIG4      = 0x18,
    REG_FXLS8964_SENS_CONFIG5      = 0x19,
    REG_FXLS8964_WAKE_IDLE_LSB     = 0x1A,
    REG_FXLS8964_WAKE_IDLE_MSB     = 0x1B,
    REG_FXLS8964_SLEEP_IDLE_LSB    = 0x1C,
    REG_FXLS8964_SLEEP_IDLE_MSB    = 0x1D,
    REG_FXLS8964_ASLP_COUNT_LSB    = 0x1E,
    REG_FXLS8964_ASLP_COUNT_MSB    = 0x1F,
    REG_FXLS8964_INT_EN            = 0x20,
    REG_FXLS8964_INT_PIN_SEL       = 0x21,
    REG_FXLS8964_OFF_X             = 0x22,
    REG_FXLS8964_OFF_Y             = 0x23,
    REG_FXLS8964_OFF_Z             = 0x24,
    REG_FXLS8964_BUF_CONFIG1       = 0x26,
    REG_FXLS8964_BUF_CONFIG2       = 0x27,
    REG_FXLS8964_ORIENT_STATUS     = 0x28,
    REG_FXLS8964_ORIENT_CONFIG     = 0x29,
    REG_FXLS8964_ORIENT_DBCOUNT    = 0x2A,
    REG_FXLS8964_ORIENT_BF_ZCOMP   = 0x2B,
    REG_FXLS8964_ORIENT_THS_REG    = 0x2C,
    REG_FXLS8964_SDCD_INT_SRC1     = 0x2D,
    REG_FXLS8964_SDCD_INT_SRC2     = 0x2E,
    REG_FXLS8964_SDCD_CONFIG1      = 0x2F,
    REG_FXLS8964_SDCD_CONFIG2      = 0x30,
    REG_FXLS8964_SDCD_OT_DBCNT     = 0x31,
    REG_FXLS8964_SDCD_WT_DBCNT     = 0x32,
    REG_FXLS8964_SDCD_LTHS_LSB     = 0x33,
    REG_FXLS8964_SDCD_LTHS_MSB     = 0x34,
    REG_FXLS8964_SDCD_UTHS_LSB     = 0x35,
    REG_FXLS8964_SDCD_UTHS_MSB     = 0x36,
    REG_FXLS8964_SELF_TEST_CONFIG1 = 0x37,
    REG_FXLS8964_SELF_TEST_CONFIG2 = 0x38,
};



#define FXLS8964_SENS_CONFIG1_FSR_2G 0b00
#define FXLS8964_SENS_CONFIG1_FSR_4G 0b01
#define FXLS8964_SENS_CONFIG1_FSR_8G 0b10
#define FXLS8964_SENS_CONFIG1_FSR_16G 0b11
#define FXLS8964_SENS_CONFIG1_ACTIVE_STANDBY 0b0 
#define FXLS8964_SENS_CONFIG1_ACTIVE_WAKE 0b1



#endif