#ifndef LED_DRIVER_H
#define LED_DRIVER_H

#include <Arduino.h>

const int numLEDs = 8; //Number of LEDs on the LED board.

// Function prototypes
void resetLEDTimeout(int channel);
void resetLEDs(bool hardwareActive = true);
float readLEDOutput(int channel, bool current = true);
void setLEDVoltageOutput(int channel, bool current = true);
// void setLEDFeedbackSignal(int channel, bool current = true); // Shouldt access this one externally as it i taken care of in setLEDLevel
// void setLEDLevelRaw(int channel, float voltage); // Shouldn't access this one externally.
void setLEDLevel(int channel, float power = 0.0, bool current = true);
void calibrateLED(int channel,float maxCurrent = 5.0);
void turnLEDsOff();
void switchLEDDirect(int channel, bool on = false, bool force = false);
void switchLEDTimed(int channel, float time = 0.0, bool on = false);
void LEDTimingHandler();
void measureLED(int channel, float* result);
void getLEDSetup(int channel, float* result);
float measurePhotodiode(int channel);

#endif // LED_DRIVER_H