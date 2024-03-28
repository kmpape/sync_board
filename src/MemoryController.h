#ifndef MEMORYCONTROLLER_H
#define MEMORYCONTROLLER_H

#include "Arduino.h"

extern int bytes_offset;
extern int float_size;
extern int max_address;

//Current Configuration
//float addresses 0-47 are taken by the LED Calibration levels in LEDDriver.

/**
 * @brief Write a float (single precision), valid addresses 0-511
 * 
 * @param address   The address to write to
 * @param value     The value to write
 */
void fwrite(int address, float value); // Write a float (single precision), valid addresses 0-511

/**
 * @brief Read a float (single precision), valid addresses 0-511
 * 
 * @param address The address to read from
 * @return float The value read
 */
float fread(int address); // read a float (single precision), valid addresses 0-511

/**
 * @brief Write an int (16 bit), valid addresses 0-1117
 * 
 * @param address The address to write to
 * @param value The value to write
 */
void iwrite(int address, int value); //Write an int (16 bit), valid addresses 0-1117

/**
 * @brief Read an int (16 bit), valid addresses 0-1117
 * 
 * @param address The address to read from
 * @return int The value read
 */
int iread(int address); //read an int (16 bit), valid addresses 0-1117

/**
 * @brief clear all of the memory (floats and ints)
 * 
 */
void clearMemory();  // Set everything to zero


#endif