#ifndef _TMP117_H_
#define _TMP117_H_
#include <Arduino.h>
#include <Wire.h>  //includes wire library//

#include "I2CAddressBook.h"


class TMP117 {
  public:
    TMP117();
    // speficy I2C bus addressing for our purpose//
    TMP117(uint8_t addr);  // intialize using address
    void Initialise();      //initialisation function
    float getTemp();    // retrieves the latest temperature variable
    void updateTemp(); // queries the device and updates latest_temperature variable
  private:
    float latest_temperature = -1; // latest temperature reading in celsius
    // the i2c address
    uint8_t i2caddr;
};


#endif

