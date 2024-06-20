// MagnetDriver.h
#pragma once

void switchMagnetOutput(bool NC = true);
void switchMagnetRelay(bool closed);
void attachMagnetBoard();
void calibrateMagnet();
void setupMagnetBoard();
float singleReadMagnetADC(uint8_t channel);