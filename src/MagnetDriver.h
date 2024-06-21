// MagnetDriver.h
#pragma once

void calibrateHall(int hall_id=0);
void setMagnetField(int NC, float field);
float singleReadHall(uint8_t id);
float singleReadVCurrent();
float ADC_V_to_mT(float Vout);
void switchMagnetOutput(bool NC = true);
void switchMagnetRelay(bool closed);
void attachMagnetBoard();
void calibrateMagnet();
void setupMagnetBoard();
float singleReadMagnetADC(uint8_t channel);
void setMagnetCurrent(int NC, float current);
void enableMagnet(bool enable);