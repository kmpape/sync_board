#include <Arduino.h>
#include "i2c_adc_ads7828.h"
#include "GlobalVariables.h"
#include "ICDrivers.h"
#include "SerialController.h"
#include "MagnetDriver.h"

bool adcEnabled = false;

// Channel mask determines which channels are enabled
// e.g. 0x01 enables channel 0, 0x02 enables channel 1, 0x03 enables channels 0 and 1
// 0xFF enables all channels
uint8_t CHANNELS_MASK = 0x01;

ADS7828 device(0, SINGLE_ENDED | REFERENCE_OFF | ADC_ON, CHANNELS_MASK);
ADS7828* adc = &device;

// Could do this for known channels
// ADS7828Channel* channel0 = adc->channel(0);

void setupMagnetBoard() {
    if (MagAttached == false) {
        raiseError("MagnetBoard: No Magnet Board Attached");
        return;
    }
    
    // Could do things like configure channel min/max scales
    // adc->channel(0)->minScale = 0;
    // adc->channel(0)->maxScale = 0xFFFF; // Just return the full scale value for now
    adcEnabled = true;
}

uint8_t singleReadMagnetADC(uint8_t channel) {
    if (MagAttached == false) {
        raiseError("MagnetBoard: No Magnet Board Attached");
        return 0;
    }
    if (adcEnabled == false) {
        raiseError("MagnetBoard: ADC not enabled. Enable it using setupMagnetBoard()");
        return 0;
    }
    if (channel < 0 || channel > 7) {
        raiseError("MagnetBoard: Tried to read from invalid ADC channel "+String(channel)+".");
        return 0;
    }
    if ((CHANNELS_MASK & (1 << channel)) == 0) {
        raiseError("MagnetBoard: Tried to read from invalid ADC channel "+String(channel)+". Channel is not enabled (check CHANNELS_MASK)");
        return 0;
    }
    adc->update();
    return adc->channel(channel)->sample(); // Returns the last sample
}