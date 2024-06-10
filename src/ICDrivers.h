// ICDrivers.h

#ifndef ICDRIVERS_H
#define ICDRIVERS_H

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>

extern uint8_t SyncBoard_PWM_LVLShift_ADR;
extern uint8_t SyncBoard_PWM_Switches_ADR;
extern uint8_t SyncBoard_ADC_ADR;
extern uint8_t LED_ADC_ADR;
extern uint8_t LED_DAC_ADR;
extern uint8_t LED_PWM_ADR;
extern uint8_t MagBoard_ADC_ADR;
extern uint8_t MagBoard_DAC_ADR;

void I2CWrite(int address, uint8_t* data, size_t length);
void I2CWrite(int address, uint8_t data, size_t length = 1);
void I2CRead(int address, byte* buffer, size_t bytes_to_read);
byte I2CRead(int address, int read_address = -1);
void setPWM(uint8_t address, int channel, double value);
void setupPWM(uint8_t address, bool slow = false);
void setupPWMs();
uint16_t readADCOnce(int channel, bool internal = true, int ADC_ID = 0);
void setupDACSPI(bool turnOn = false);
void setDACSPI(int channel, float value);
void setupDACI2C(int addr, bool turnOn = false);
void setupLEDDACI2C(bool turnOn = false);
void setupMagDACI2C(bool turnOn = false);
void setDACI2C(int addr, int channel, float value);
void setLEDDACI2C(int channel, float value);
void setMagDACI2C(int channel, float value);
void I2CScan();

#endif // ICDRIVERS_H