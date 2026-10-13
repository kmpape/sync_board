#ifndef TCA9548_H
#define TCA9548_H

#include <Wire.h>

class TCA9548 {
public:
    TCA9548();
    TCA9548(uint8_t addr);
    void Initialise();
    void setChannel(uint8_t channel_ID);
    void printaddress();
    uint8_t activated_channel;

private:
    uint8_t i2caddr;
    uint8_t channel_state;
};

#endif // TCA9548_H