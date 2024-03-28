#ifndef IOCONTROLLER_H
#define IOCONTROLLER_H

// Include necessary libraries
#include <Arduino.h>

// Function declarations
void setupGPIO (int pin, bool disable = false, int GPIOFunction = 0,bool GPIOInput = false);
void turnICsOff();
void configureIO (bool GPIOInput[9], int GPIOFunction[9], bool GPIOEnabled[9], bool activate = true);
void debugIO(int channel, float value);
int GPIOPinMap2(int pin);
int GPIOPinMap3(int pin);
float readADC(int channel, int ADC_ID = 0);
void setDAC(int channel, float value);
void setSwitch(int channel = 0, float turnOn = 0.0);

#endif // IOCONTROLLER_H