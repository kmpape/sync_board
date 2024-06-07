// MagnetDriver.h
#pragma once

#include "i2c_adc_ads7828.h"

void setupMagnetBoard();
uint8_t singleReadMagnetADC(uint8_t channel);