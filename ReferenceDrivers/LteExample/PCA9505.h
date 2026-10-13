#ifndef _PCA9505_H_
#define _PCA9505_H_
#include <Arduino.h>
#include <Wire.h>  //includes wire library//
#include "Teensy_HID.h"
#include "PinOutIOChips.h"
#include "I2CAddressBook.h"

typedef struct st_ChipState {
  //Make a bank[5] that defaults to evertyyng off/zero
    uint8_t bank[5] = {0x00, 0x00, 0x00, 0x00, 0x00}; // default to all pins as ZERO
    //uint8_t bank[5];
} CHIPSTATE;
// array of unassigned 8 bit integers as registers for the 5 different banks//

struct PinLocation {
    uint8_t bank;
    uint8_t position;
};

class PCA9505 {
  public:
    PCA9505();
    // speficy I2C bus addressing for our purpose//
    PCA9505(uint8_t addr);  // intialize using address
    void Initialise();      //initialisation function

    void setPinMode(uint8_t pin, bool state);

    uint8_t getLocalBankData(uint8_t bank);

    // input and outputs for pins or ports//
    bool getPinMode(uint8_t pin);  // User to check the pins mode
    uint8_t getBankMode(uint8_t bank);
    CHIPSTATE getChipMode();  // retrieves the I/O configuration of all the pins on the chip, i.e. whether the pin is set to be an output or an input//

    // Configures pin's 1 and 0 states, turns pins on and off and see if pin is on or off, reading and wrting//
    void digitalPinWrite(uint8_t pin, uint8_t dir);       // pin level//
    void digitalBankWrite(uint8_t bank, uint8_t bank_dir);  // bank level// creates an array of 8 values for each pin//
    void digitalChipWrite(CHIPSTATE chip);                // chip level //

    uint8_t digitalPinRead(uint8_t pin);
    uint8_t digitalBankRead(uint8_t bank);
    CHIPSTATE digitalChipRead();

    // Setting interrupts//
    void setPinInterrupt(uint8_t pin, bool enable);       // boolean as true or false to enable or disable interrupts for pins//
    void setBankInterrupt(uint8_t bank, uint8_t enable);  // setting bank interrupts
    void setChipInterrupt(CHIPSTATE chip);                // setting the entire chip at once to enable interrupts on all ports set to be inputs

    uint8_t getPinInterrupt(uint8_t pin);    // check if a pin has interrupt?
    uint8_t getBankInterrupt(uint8_t bank);  // reading which pins are interrupts
    CHIPSTATE getChipInterrupt();            // reading whether or not interrupts are enabled on a pin for the entire chip at once

    void clearInterrupt();  // resetting

    // Polarity inversion//
    void setPinPolarity(uint8_t pin, uint8_t toggle);
    void setBankPolarity(uint8_t bank, uint8_t toggle);
    void setChipPolarity(CHIPSTATE chip);

    uint8_t getPinPolarity(uint8_t pin);
    uint8_t getBankPolarity(uint8_t bank);
    CHIPSTATE getChipPolarity();
    void sendChipData(uint8_t reg, CHIPSTATE chip);

  private:
    CHIPSTATE input_output_config;  // Array to store whether the pins across the 5 banks are set to be an input or output
    CHIPSTATE IO_state;             // Array to store the IO state of the pins across the 5 banks, to prevent having to query the devvice each time
    CHIPSTATE interrupt_config;     // Array to store the interrupt configuration of the pins across the 5 banks
    PinLocation getPinLocation(uint8_t pin);
    
    CHIPSTATE translateIOConfig(const pinout_io_config_e (*pinout)[8]);
    CHIPSTATE translateInterruptConfig(const pinout_io_config_e (*pinout)[8]);
    const pinout_io_config_e (*PINOUT)[8];

    void sendBankData(uint8_t reg, uint8_t bank, uint8_t data);

    uint8_t retrieveBankData(uint8_t reg, uint8_t bank);
    CHIPSTATE retrieveChipData(uint8_t reg);
    // the i2c address
    uint8_t i2caddr;
};

// auto incrementation of bank reading able and disable
#define AI_ON 0x80
#define AI_OFF 0x00

// high low states for bulk management//
#define IO_HIGH 0Xff
#define IO_LOW 0x00
#define IO_INPUT 1
#define IO_OUTPUT 0

// Bank Registers
//  Input Port Register (read only, reads what signal is coming in)
#define REG_IP 0x00  // base address for the IP register
#define REG_IP0 0x00
#define REG_IP1 0x01
#define REG_IP2 0x02
#define REG_IP3 0x03
#define REG_IP4 0x04

// Outpot Port Register (read or write to toggle the pins on or off)
#define REG_OP 0x08
#define REG_OP0 0x08
#define REG_OP1 0x09
#define REG_OP2 0x0A
#define REG_OP3 0x0B
#define REG_OP4 0x0C

// Polarity Inversion Register (only applicable to ports set as inputs)
#define REG_PI 0x10
#define REG_PI0 0x10
#define REG_PI1 0x11
#define REG_PI2 0x12
#define REG_PI3 0x13
#define REG_PI4 0x14

// I/O Configuration Register (sets whether or not a pin is an input or output)
#define REG_IOC 0x18
#define REG_IOC0 0x18
#define REG_IOC1 0x19
#define REG_IOC2 0x1A
#define REG_IOC3 0x1B
#define REG_IOC4 0x1C

// Mask Interrupt Register (used to define whether or not an input pin will generate an interrupt when its polarity changes - default is none do)
// When the interrupt is triggered this activates the INT pin, and it is only cleared when the pin is read
#define REG_MSK 0x20
#define REG_MSK0 0x20
#define REG_MSK1 0x21
#define REG_MSK2 0x22
#define REG_MSK3 0x23
#define REG_MSK4 0x24

#endif

/** to do:
 * can consider including i2c scan to display all the pca9505s listed
 *
 */
