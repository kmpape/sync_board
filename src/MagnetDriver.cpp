#include <Arduino.h>
#include "GlobalVariables.h"
#include "ICDrivers.h"
#include "SerialController.h"
#include "MagnetDriver.h"

bool adcEnabled = false;
bool dacEnabled = false;

void setupMagnetBoard() {
    if (MagAttached == false) {
        raiseError("MagnetBoard: No Magnet Board Attached");
        return;
    }
    
    if (checkForDevice(MagBoard_ADC_ADR) != 0) {
        raiseError("MagnetBoard: No ADC detected at address 0x"+String(MagBoard_ADC_ADR, HEX));
    } else {
        adcEnabled = true;
    }

    if (checkForDevice(MagBoard_DAC_ADR) != 0) {
        raiseError("MagnetBoard: No DAC detected at address 0x"+String(MagBoard_DAC_ADR, HEX));
    } else {
        setupDACI2C(MagBoard_DAC_ADR, true);
        dacEnabled = true;
    }

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
    if (channel > 7) {
        raiseError("MagnetBoard: Tried to read from invalid ADC channel "+String(channel)+".");
        return 0;
    }
    return readADCOnce(channel, false, 2);
}