#include "MemoryController.h"
#include "Arduino.h"
#include "EEPROM.h"
#include "GlobalVariables.h"

// This is a very high tech memory controller.
// The device has a massive 4284 bytes of EEPROM memory.
// We split this up as 512 floats and 1118 ints.
// The floats are addressed first and then the ints so the ints have an offset to their address implemented below.
// If we need logic flags in future could convert some of the ints into 16*bools.

int float_size = 4; // 4 bytes per float in arduino
int float_max_address = 511;
// int float_endpoint=float_max_address*float_size; // Everything before this address is a float (or part of one)

int int_startpoint = (float_max_address+1)*float_size; // Everything >= this address is an int (or part of one)
int int_size = 2; // 2 bytes per int in arduino
int int_max_address = 1117;
//int int_endpoint = int_max_address*int_size; // 1118 ints * 2 bytes per int. 
//int bytes_offset = max_address; // This isnt used, here for completeness.

int max_address = 4284; // 4284 bytes of EEPROM memory for Teensy4.1


void fwrite(int address, float value) {
  // write a float to the EEPROM
  if (address>float_max_address) {
    Serial.println(":Error: float address out of range");
    address=float_max_address;
  }
  address=address*float_size; // Starting address of this byte

  byte* bytePtr = (byte*)(void*)&value;
  for (int i = 0; i < float_size; i++) {
    EEPROM.write(address + i, bytePtr[i]);
  }
}

float fread(int address) {
  // read a float from the EEPROM
  if (address>float_max_address) {
    Serial.println(":Error: float address out of range");
    address=float_max_address;
  }
  address=address*float_size; // We do this so it is easier to use the function i.e. consecutive floats are at consective bytes
  
  float value = 0.0;
  byte* bytePtr = (byte*)(void*)&value;
  for (int i = 0; i < float_size; i++) {
    bytePtr[i] = EEPROM.read(address + i);
  }

  return value;
}





void iwrite(int address, int value) {
  // write a float to the EEPROM
  if (address>int_max_address) {
    Serial.println(":Error: int address out of range");
    address=int_max_address;
  }
  address=int_startpoint + address*int_size; // Starting address of this int

  byte* bytePtr = (byte*)(void*)&value;
  for (int i = 0; i < int_size; i++) {
    EEPROM.write(address + i, bytePtr[i]);
  }
}

int iread(int address) {
  // read a float from the EEPROM
  if (address>int_max_address) {
    Serial.println(":Error: int address out of range");
    address=int_max_address;
  }

  address=int_startpoint + address*int_size; // Starting address of this int
  
  int value = 0;
  byte* bytePtr = (byte*)(void*)&value;
  for (int i = 0; i < float_size; i++) {
    bytePtr[i] = EEPROM.read(address + i);
  }

  return value;
}









// void bwrite(int address, int value) {
//     // NOTE this function isnt used. It is here for completeness.
//     if (address>=max_address) {
//         Serial.println(":Error: byte address out of range");
//         address=max_address-1;
//     }
//     address=address+bytes_offset;
//     if (value > 255) {
//         Serial.println(":Error: value too large for byte read");
//         value = 255;
//     }

//     EEPROM.write(address, value);
// }

// byte bread(int address) {
//     // read a byte from the EEPROM
//     // NOTE this function isnt used. It is here for completeness.
//     address=address+bytes_offset; // Offset of bytes storage location
//     if (address>=max_address) {
//         Serial.println(":Error: byte address out of range");
//         address=max_address-1;
//     }

//     return EEPROM.read(address);
// }



void clearMemory() {
    // Note this deletes EVERYTHING in the memory so be careful.
    // Clear all the floats
    for (int i = 0; i <= float_max_address; i += 1) {
        fwrite(i, 0.0);
    }

    // Clear all the bytes
    for (int i = 0; i < int_max_address; i++) {
        iwrite(i, 0);
    }

    //Now just to confirm, and indeed this should be redudant, we set the whole EEPROM to zero
    for (int i = 0; i < max_address; i++) {
        EEPROM.write(i, 0);
    }
}