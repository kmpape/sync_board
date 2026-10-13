#include "TCA9548.h"
#include "Teensy_HID.h"

TCA9548::TCA9548() : i2caddr(0){
    teensy_device.sendError("Bad key provided for TCA9548");
}

TCA9548::TCA9548(uint8_t addr) : i2caddr(addr){
}

void TCA9548::setChannel(uint8_t channel_ID) {
    activated_channel = channel_ID;
    channel_state = 0;
    channel_state |= (1 << channel_ID);

    Wire1.beginTransmission(i2caddr);
    
    Wire1.write(channel_state);
   uint8_t result = Wire1.endTransmission();
    if (result != 0) {
        teensy_device.sendError("TCA9548: Failed to set channel, Error code: %d", result);
    }
}

void TCA9548::Initialise(){
    setChannel(0); // Set multiplexer to channel 0 by default
}