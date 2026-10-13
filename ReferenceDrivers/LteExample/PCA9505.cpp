#include "PCA9505.h"
#include "Teensy_HID.h"

// Default constructor
PCA9505::PCA9505() : i2caddr(0) {
    teensy_device.sendError("Bad key provided for PCA9505");
    // Default initialization if needed
}

// Directly assigning raw address and some initilization of the chip//
// Interrupt all set to 1 - all masked//
PCA9505::PCA9505(uint8_t addr) {
    i2caddr = addr;
    // Dependent on which chip is being initialised the pins across the 5 banks need to be configured to the appropriate input/output state.
    // This assigns one 8 bit number for each bank, to configure if each pin is either a '0' output or '1' input
    // The interrupt config is 1 if input pin state change does not trigger interrupt, or 0 if it does.
    if (i2caddr == Senseboard_IO_1_ADR) {
        PINOUT = Senseboard_IO_1_PINOUT;        
        
    } else if (i2caddr == Topboard_IO_1_ADR) {
        PINOUT = Topboard_IO_1_PINOUT;   

    } else if (i2caddr == Topboard_IO_2_ADR) {
        PINOUT = Topboard_IO_2_PINOUT;  

    } else if (i2caddr == Heatboard_IO_1_ADR) {
        PINOUT = Heatboard_IO_1_PINOUT;  

    } else if (i2caddr == Heatboard_IO_2_ADR) {
        PINOUT = Heatboard_IO_2_PINOUT;  

    } else if (i2caddr == Heatboard_IO_3_ADR) {
        PINOUT = Heatboard_IO_3_PINOUT;  

    } else if (i2caddr == Heatboard_IO_4_ADR) {
        PINOUT = Heatboard_IO_4_PINOUT;  

    } else {
        teensy_device.sendError("Invalid I2C address for PCA9505: %X", addr);
    }

    input_output_config = translateIOConfig(PINOUT);
    interrupt_config = translateInterruptConfig(PINOUT);
}

CHIPSTATE PCA9505::translateIOConfig(const pinout_io_config_e (*pinout)[8]){
    // depending what is defined in PinOutIOCHips for each pin, we configure the chip to reflect this
    //  0 for output,
    //  1 for input
    //  1 for interrupt
    CHIPSTATE io_config;

    for (int bank = 0; bank < 5; bank++) {
        uint8_t bank_config = 0;
        for (int pin = 0; pin < 8; pin++) {
            if (pinout[bank][pin] == 0) { // assign a 0 for output
                bank_config |= (0 << pin);
            } else {
                bank_config |= (1 << pin); // assign a 1 for input/interrupt
            }
        }
        io_config.bank[bank] = bank_config;
    }   

    return io_config;
}
CHIPSTATE PCA9505::translateInterruptConfig(const pinout_io_config_e (*pinout)[8]){
    // depending what is defined in PinOutIOCHips for each pin, we configure the chip to reflect this
    //  0 for interrupt,
    //  1 for input
    //  1 for output
    
    CHIPSTATE int_config;

    for (int bank = 0; bank < 5; bank++) {
        uint8_t bank_config = 0;
        for (int pin = 0; pin < 8; pin++) {
            if (pinout[bank][pin] > 1) {
                bank_config |= (0 << pin);
            } else {
                bank_config |= (1 << pin);
            }
        }
        int_config.bank[bank] = bank_config;
    }   

    return int_config;
}

void PCA9505::Initialise() {
    //see schematic docs to see the state of each pin - here is the initialisation where each pin is set to their relative inputs and outputs

    sendChipData(REG_IOC, input_output_config);  // set which pins are inputs and outputs
    CHIPSTATE chipzeros; 
    for (int i = 0; i < 5; i++) {
        chipzeros.bank[i] = 0;  // set all the banks to 0
    }
    sendChipData(REG_OP, chipzeros);  // make sure all outputs are 0

    // if commented out then INT pin should never go low because no inputs are set to trigger interrupts by default
    // sendChipData(REG_MSK, interrupt_config);  //using the write bank function to set which input pins trigger an interrupt
}


PinLocation PCA9505::getPinLocation(uint8_t pin) {
    PinLocation loc;
    switch (pin) {
        case 3:
            loc.bank = 0;
            loc.position = 0;
            break;
        case 4:
            loc.bank = 0;
            loc.position = 1;
            break;
        case 5:
            loc.bank = 0;
            loc.position = 2;
            break;
        case 7:
            loc.bank = 0;
            loc.position = 3;
            break;
        case 8:
            loc.bank = 0;
            loc.position = 4;
            break;
        case 9:
            loc.bank = 0;
            loc.position = 5;
            break;
        case 10:
            loc.bank = 0;
            loc.position = 6;
            break;
        case 12:
            loc.bank = 0;
            loc.position = 7;
            break;
        case 13:
            loc.bank = 1;
            loc.position = 0;
            break;
        case 14:
            loc.bank = 1;
            loc.position = 1;
            break;
        case 15:
            loc.bank = 1;
            loc.position = 2;
            break;
        case 16:
            loc.bank = 1;
            loc.position = 3;
            break;
        case 17:
            loc.bank = 1;
            loc.position = 4;
            break;
        case 19:
            loc.bank = 1;
            loc.position = 5;
            break;
        case 20:
            loc.bank = 1;
            loc.position = 6;
            break;
        case 21:
            loc.bank = 1;
            loc.position = 7;
            break;
        case 22:
            loc.bank = 2;
            loc.position = 0;
            break;
        case 24:
            loc.bank = 2;
            loc.position = 1;
            break;
        case 25:
            loc.bank = 2;
            loc.position = 2;
            break;
        case 26:
            loc.bank = 2;
            loc.position = 3;
            break;
        case 31:
            loc.bank = 2;
            loc.position = 4;
            break;
        case 32:
            loc.bank = 2;
            loc.position = 5;
            break;
        case 33:
            loc.bank = 2;
            loc.position = 6;
            break;
        case 35:
            loc.bank = 2;
            loc.position = 7;
            break;
        case 36:
            loc.bank = 3;
            loc.position = 0;
            break;
        case 37:
            loc.bank = 3;
            loc.position = 1;
            break;
        case 38:
            loc.bank = 3;
            loc.position = 2;
            break;
        case 40:
            loc.bank = 3;
            loc.position = 3;
            break;
        case 41:
            loc.bank = 3;
            loc.position = 4;
            break;
        case 42:
            loc.bank = 3;
            loc.position = 5;
            break;
        case 43:
            loc.bank = 3;
            loc.position = 6;
            break;
        case 44:
            loc.bank = 3;
            loc.position = 7;
            break;
        case 45:
            loc.bank = 4;
            loc.position = 0;
            break;
        case 47:
            loc.bank = 4;
            loc.position = 1;
            break;
        case 48:
            loc.bank = 4;
            loc.position = 2;
            break;
        case 49:
            loc.bank = 4;
            loc.position = 3;
            break;
        case 50:
            loc.bank = 4;
            loc.position = 4;
            break;
        case 52:
            loc.bank = 4;
            loc.position = 5;
            break;
        case 53:
            loc.bank = 4;
            loc.position = 6;
            break;
        case 54:
            loc.bank = 4;
            loc.position = 7;
            break;
        default:
            teensy_device.sendError("Invalid pin number: %d", pin);
            loc.bank = 0xFF;      // Indicates an invalid bank
            loc.position = 0xFF;  // Indicates an invalid position
            break;
    }
    return loc;
}

// configure banks (private)// //can maybe optimise the data parameter for user?? see register addresses in header
void PCA9505::sendBankData(uint8_t reg, uint8_t bank, uint8_t data) {

    int num_tries_remaining = 5;
    uint8_t result = 1; // non-zero to enter the loop
    while ((result != 0) && (num_tries_remaining > 0)) {
        Wire1.beginTransmission(i2caddr);
        Wire1.write((reg + bank) | AI_OFF);
        Wire1.write(data);
        
        result = Wire1.endTransmission();
        if (result != 0) {
            // teensy_device.sendError("Failed to send bank data to PCA9505 0x%X code: %d", i2caddr, transmission_fail);
            teensy_device.sendError("PCA9505 0x%X: failed set bank %X to %X, retry %d", i2caddr, reg + bank, data, num_tries_remaining);
            // teensy_device.sendInfo("reg: %X bnk: %X", reg, bank);
            num_tries_remaining--;
        }
    }
}

// send and write data to each bank in a register sequentially//
//can use this to config all the banks in a single register (entire chip) in one go with the CHIPSTATE object
//Command should go something like this - sendChipData(register address, {hex,hex,hex,hex,hex});
void PCA9505::sendChipData(uint8_t reg, CHIPSTATE chip) {
    Wire1.beginTransmission(i2caddr);
    Wire1.write(reg | AI_ON);
    Wire1.write(chip.bank[0]);
    Wire1.write(chip.bank[1]);
    Wire1.write(chip.bank[2]);
    Wire1.write(chip.bank[3]);
    Wire1.write(chip.bank[4]);
    uint8_t transmission_fail = Wire1.endTransmission();
    if (transmission_fail != 0) {
        teensy_device.sendError("PCA9505 0x%X: Failed to set chip reg: 0x%X", i2caddr, reg);
    }
}

// read the state of each bank according to register (private)//
uint8_t PCA9505::retrieveBankData(uint8_t reg, uint8_t bank) {
    Wire1.beginTransmission(i2caddr);
    Wire1.write((reg + bank) | AI_OFF);
    Wire1.endTransmission();

    Wire1.requestFrom(i2caddr, (uint8_t)1);  // request to read one byte of data
    return Wire1.read();
}

// read from a register sequentially and return the data on all banks  //
//This will give you the state of all the pins in the resgiter specified//
CHIPSTATE PCA9505::retrieveChipData(uint8_t reg) {
    CHIPSTATE data = {0};
    Wire1.beginTransmission(i2caddr);
    Wire1.write(reg | AI_ON);
    Wire1.endTransmission();

    Wire1.requestFrom(i2caddr, (uint8_t)5);

    data.bank[0] = Wire1.read();
    data.bank[1] = Wire1.read();
    data.bank[2] = Wire1.read();
    data.bank[3] = Wire1.read();
    data.bank[4] = Wire1.read();
    return data;
}

void PCA9505::setPinMode(uint8_t pin, bool state) {
    // A pin is set to input mode when its value is equal to 1, and output when equal to 0
    uint8_t set = retrieveBankData(REG_IOC, getPinLocation(pin).bank);
    bitWrite(set, getPinLocation(pin).position, (state));
    
    sendBankData(REG_IOC, getPinLocation(pin).bank, set);
}

// checks the bit value of the pin state against the register state//
//will tell you if a pin is set to input or output when queries with the bank and pin number//
bool PCA9505::getPinMode(uint8_t pin) {
    return (retrieveBankData(REG_IOC, getPinLocation(pin).bank) & (1 << getPinLocation(pin).position)) ? IO_INPUT : IO_OUTPUT;
}

//similar to above but bank level//
uint8_t PCA9505::getBankMode(uint8_t bank) {
    return (retrieveBankData(REG_IOC, bank));
}

// bulk reading of all the current states for the different banks as input and output, reversed logic for easy interpretations// //I think reversing the logic could just lead to confusion if its the opposite to the datasheet, I think 0 for Output and 1 for Input logically makes sense
CHIPSTATE PCA9505::getChipMode() {
    CHIPSTATE data = retrieveChipData(REG_IOC);
    return data;
}

uint8_t PCA9505::getLocalBankData(uint8_t bank) {
    return IO_state.bank[bank];
}

// only to output pins to manipulate their onoff states, confused bcs HIGH LOW are hex, but here it seems to be only amending one bit in byte//
// TESTING REQUIRED!!//
void PCA9505::digitalPinWrite(uint8_t pin, uint8_t dir) {
    // TODO: Should we in here have some check to ensure that the bank/pin you are messing with is indeed one that is set to be an output? Would need to be updated based on some config file which is unique to each chip
    uint8_t bank_data = IO_state.bank[getPinLocation(pin).bank];
    bitWrite(bank_data, getPinLocation(pin).position, dir); // modifies bank_data to set the pin at that position to dir
    IO_state.bank[getPinLocation(pin).bank] = bank_data;  // update the IO state to reflect the new state of the pin
    sendBankData(REG_OP, getPinLocation(pin).bank, bank_data);
}

// bulk turining on and off bank level//
//Writing to the output pins to toggle them on or off at bank level//
//format is bank number (0-4) and an 8 bit number to configure the 8 pins in that bank//
void PCA9505::digitalBankWrite(uint8_t bank, uint8_t bank_dir) {
    IO_state.bank[bank] = bank_dir;  // update the IO state to reflect the new state of the bank
    sendBankData(REG_OP, bank, bank_dir);
}

// writes to all 5 ports at once using an array//
//can configure all the banks in the output register at once to toggle them on or off using the CHIPSTATE structure (a 8 by 5 array)//
void PCA9505::digitalChipWrite(CHIPSTATE chip) {
    IO_state = chip;  // update the IO state to reflect the new state of the chip
    sendChipData(REG_OP, chip);
}

// reading inputs fron input port pins//
uint8_t PCA9505::digitalPinRead(uint8_t pin) {
    return (retrieveBankData(REG_IP, getPinLocation(pin).bank) & (1 << getPinLocation(pin).position)) ? IO_HIGH : IO_LOW;
}

// Reading input from ports//
uint8_t PCA9505::digitalBankRead(uint8_t bank) {
    return retrieveBankData(REG_IP, bank);
}

CHIPSTATE PCA9505::digitalChipRead() {
    return retrieveChipData(REG_IP);
}

void PCA9505::setPinInterrupt(uint8_t pin, bool enable = true) {
    uint8_t set = retrieveBankData(REG_MSK, getPinLocation(pin).bank);
    bitWrite(set, getPinLocation(pin).position, (enable ? 0 : 1));
    sendBankData(REG_MSK, getPinLocation(pin).position, set);
}

void PCA9505::setBankInterrupt(uint8_t bank, uint8_t enable) {
    sendBankData(REG_MSK, bank, enable);
}

void PCA9505::setChipInterrupt(CHIPSTATE chip) {
    sendChipData(REG_MSK, chip);
}

uint8_t PCA9505::getPinInterrupt(uint8_t pin) {
    return (retrieveBankData(REG_MSK, getPinLocation(pin).bank) >> pin) & 0x01;
}

uint8_t PCA9505::getBankInterrupt(uint8_t bank) {
    return retrieveBankData(REG_MSK, bank);
}

CHIPSTATE PCA9505::getChipInterrupt() {
    return retrieveChipData(REG_MSK);
}

void PCA9505::clearInterrupt() {
    CHIPSTATE io_chip_state_int = digitalChipRead();
    teensy_device.sendError("Chip 0x%X State:%X %X %X %X %X", i2caddr, io_chip_state_int.bank[0], io_chip_state_int.bank[1], io_chip_state_int.bank[2], io_chip_state_int.bank[3], io_chip_state_int.bank[4]);
    bool is_fault_triggered = false;
    for (int bank_id=0; bank_id<5; bank_id++){
        for (int position_id=0; position_id<8; position_id++){
            is_fault_triggered = ((io_chip_state_int.bank[bank_id] >> position_id) && 0x0); // all are pulled to logic low if a fault occurs 
            if (is_fault_triggered) {
                if (PINOUT[bank_id][position_id] == cfg_FAULT_STIR){ 
                    teensy_device.sendError("Stirring fault, chp: 0x%X, bnk: %d, chnl: %d", i2caddr, bank_id, position_id);
                } else if (PINOUT[bank_id][position_id] == cfg_FAULT_HEATER){ 
                    teensy_device.sendError("Heater fault, chp: 0x%X, bnk: %d, chnl: %d", i2caddr, bank_id, position_id);
                } else if (PINOUT[bank_id][position_id] == cfg_FAULT_VALVE){
                    teensy_device.sendError("Valve fault, chp: 0x%X, bnk: %d, chnl: %d", i2caddr, bank_id, position_id);
                } else if (PINOUT[bank_id][position_id] == cfg_FAULT_EXT){
                    teensy_device.sendError("External IO fault, chp: 0x%X bnk: %d chnl: %d", i2caddr, bank_id, position_id);
                } else if (PINOUT[bank_id][position_id] == cfg_FAULT_FANS){
                    teensy_device.sendError("Fan fault, chp: 0x%X, bnk: %d, chnl: %d", i2caddr, bank_id, position_id);
                };
            }
        }
    }
    
}

void PCA9505::setPinPolarity(uint8_t pin, uint8_t toggle) {
    uint8_t set = retrieveBankData(REG_PI, getPinLocation(pin).bank);
    bitWrite(set, getPinLocation(pin).position, (toggle ? 1 : 0));
    sendBankData(REG_PI, getPinLocation(pin).bank, set);
}

void PCA9505::setBankPolarity(uint8_t bank, uint8_t toggle) {
    sendBankData(REG_PI, bank, toggle);
}

void PCA9505::setChipPolarity(CHIPSTATE chip) {
    sendChipData(REG_PI, chip);
}

uint8_t PCA9505::getPinPolarity(uint8_t pin) {
    return (retrieveBankData(REG_PI, getPinLocation(pin).bank) >> getPinLocation(pin).position) & 0x01;
}

uint8_t PCA9505::getBankPolarity(uint8_t bank) {
    return retrieveBankData(REG_PI, bank);
}

CHIPSTATE PCA9505::getChipPolarity() {
    return retrieveChipData(REG_PI);
}

/* To do:
 * HIgh and low are defines in hex, why? See digital write sections
 * We also need to think about the OE pin for PWM generation - see 7.7
 * Correlating pins to positions in registries and giving the pin names (high level api? chip specific)
 * can make a seperate file storing all the chip information
 * Correlate outputs from getPortMode,getioMode etc... to input and outputs
 * Annotate the array given by retrieve IOdata so it makes sense in terms of on off states and in context of the registry.
 * Again, the above but for writing to the IO
 * Ultimate goal - have something where you can ask for the states for pins on that device and it will give you back the state of each pi
 */