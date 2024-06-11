// This library is for all the driers for I2C and SPI devices. This means it has drivers for various  kinds of chips but I put them all together so we dont have a million includes.
#include <Arduino.h>
#include "PinOut.h"
#include "GlobalVariables.h"
#include "SerialController.h"
#include <SPI.h>
#include <Wire.h>
#include "digitalWriteFast.h"

////Address book. Naming is Board_Device_ADR
//I2C
uint8_t SyncBoard_PWM_LVLShift_ADR = 0x60; //This is the PWM chip that drives the level shifters. It is on the sync board.
uint8_t SyncBoard_PWM_Switches_ADR = 0x40; //This is the PWM chip that drives the 12V IO switches. It is on the sync board.
uint8_t SyncBoard_ADC_ADR = 0x48; //This is the ADC chip that reads the 12V IO switches. It is on the sync board.

uint8_t LED_ADC_ADR = 0x49; //This is the ADC chip that reads the LED currents. It is on the LED board.
uint8_t LED_DAC_ADR = 0x57; //This is the DAC chip that sets the LED currents. It is on the LED board.
uint8_t LED_PWM_ADR = 0x50; //This is the PWM chip that configures the LED channels. It is on the LED board.

uint8_t MagBoard_ADC_ADR = 0x4A; //This is the ADC chip that reads the magnet currents. It is on the magnet board.
uint8_t MagBoard_DAC_ADR = 0x54; //This is the DAC chip that sets the magnet currents. It is on the magnet board.

void I2CWrite(int address, uint8_t data, size_t length = 1){ // Overload in case they sendone thing at a time.
    // Writes data to i2c address.
    
    if (data < 0){
        raiseError("Sorry I2CWrite doesnt like negative numbers and you just tried to send "+String(data)+ " to address "+String(address)+". Better luck next time.");
        return;
    }
    if (data > 255){
        raiseError("Sorry I2CWrite doesnt like numbers >255 and you just tried to send "+String(data)+ " to address "+String(address)+". Better luck next time.");
        return;
    }
    
    Wire.beginTransmission(address);
    Wire.write(data);
    Wire.endTransmission();

}

void I2CWrite(int address, uint8_t* data, size_t length){
    // First check that data has the right properties i.e. no negative numbers or too large ones
    // for (int i = 0; i < length; i++){ //Below does nithing so long as data is uint8_t type as it only CAN be 0,255.
    //     if (data[i] < 0){
    //         raiseError("Sorry I2CWrite doesnt like negative numbers and you just tried to send "+String(data[i])+ " to address "+String(address)+". Better luck next time.");
    //         return;
    //     }
    //     if (data[i] > 255){
    //         raiseError("Sorry I2CWrite doesnt like numbers >255 and you just tried to send "+String(data[i])+ " to address "+String(address)+". Better luck next time.");
    //         return;
    //     }
    // }
    // Writes data to i2c address.
    Wire.beginTransmission(address);
    // Wire.write(data, length); //This doesnt work for some reason?
    for (int i = 0; i < length; i++){
        Wire.write(data[i]);
    }
    Wire.endTransmission();

}
 
void I2CScan(){
    // Scans the I2C bus and prints out the addresses of all devices found.
    byte error, address;
    int nDevices;
    Serial.println("Scanning...");
    nDevices = 0;
    for(address = 1; address < 127; address++ ) 
    {
        // The i2c_scanner uses the return value of
        // the Write.endTransmisstion to see if
        // a device did acknowledge to the address.
        Wire.beginTransmission(address);
        error = Wire.endTransmission();
        if (error == 0)
        {
            Serial.print("I2C device found at address 0x");
            if (address<16) 
                Serial.print("0");
            Serial.print(address,HEX);
            Serial.println("  !");
            nDevices++;
        }
        else if (error==4) 
        {
            Serial.print("Unknow error at address 0x");
            if (address<16) 
                Serial.print("0");
            Serial.println(address,HEX);
        }    
    }
    if (nDevices == 0) 
        Serial.println("No I2C devices found\n");
    else
        Serial.println("Done.\n");
}

uint8_t checkForDevice(int address) {
    // Checks if a device is present at a given I2C address.
    Wire.beginTransmission(address);
    return Wire.endTransmission();
}

void I2CRead(int address, byte* buffer, size_t bytes_to_read){
    // Read several bytes from I2C and you will then need to read them out of your buffer.
  
    Wire.requestFrom(address,bytes_to_read); // Request the bytes.

    // pause for a moment to let the data be transferred.
    size_t i = 0;
    while (Wire.available()){
        if (i >= bytes_to_read){
            raiseError("We got more bytes than we asked for from address "+String(address)+". We asked for "+String(bytes_to_read)+" but got "+String(i));
            return;
        }
        buffer[i] = Wire.read(); // Read the first byte 
        i++;
    }
    
    return;   
}


byte I2CRead(int address, int read_address = -1){
    // Read ONE byte from i2c address.
    //Note we can just have a blanket read (anything from that device) OR we can have one where it writes a register to be read first, then gets a response. This would be needed if you are reading the PWM chip.s

    if (read_address >=0){
        Wire.beginTransmission(address);   
        Wire.write(read_address); // First we tell it where we want to read from.
        Wire.endTransmission();
    }
    
    Wire.requestFrom(address,1); // Request the bytes.
    byte  byte1 = 0;
    while (Wire.available()){
        byte1 = Wire.read(); // Read the first byte 
    }
    
    return byte1;   
}

void setPWM(uint8_t address, int channel, double value){
    uint8_t ONL, ONH, OFFL, OFFH; // Registers on the PWM chip where data go.

    if (channel == 0) { // If we want to be turning everything off/on at once.
        ONL = 0xFA;
        ONH = 0xFB;
        OFFL = 0xFC;
        OFFH = 0xFD;
    } else {
        ONL = 2 + 4 * channel;
        ONH = 3 + 4 * channel;
        OFFL = 4 + 4 * channel;
        OFFH = 5 + 4 * channel;
    }

    if (value >= 1.0) {
        uint8_t data1[] = {ONH, 0x10};
        I2CWrite(address, data1, 2);

        uint8_t data2[] = {ONL, 0x00};
        I2CWrite(address, data2, 2);
    } else {
        uint8_t data3[] = {ONL, 0x00};
        I2CWrite(address, data3, 2);

        uint8_t data4[] = {ONH, 0x00};
        I2CWrite(address, data4, 2);
    }

    int timeOff = static_cast<int>(value * 4096) % 4096; //Converts the value to a number between 0 and 4095.
    uint8_t val3 = timeOff & 0xFF; // lower 8 bits
    uint8_t val4 = (timeOff >> 8) & 0xFF; // upper 8 bits
    uint8_t data5[] = {OFFL, val3}; //Somehow had to flip this and next command since was not working.
    I2CWrite(address, data5, 2);

    uint8_t data6[] = {OFFH, val4};
    I2CWrite(address, data6, 2);
}


void setupPWM(int address, bool slow = false){
    // Function for setting up a PWM chip. This is a generic function that can be used for any PWM chip.
    // Note this requires the I2C bus to be active! 
    // Note we COULD have a turnon flag here like the DACs,  but in fact we prefer to have it always on so that the logic low signals are going where they need (i.e. things arent floating).

    uint8_t data1[] = {0x00, 0x11}; // This turns it off. Last bit is the all-call functionality though I dont think we will use this.
    I2CWrite(address, data1, 2); 

    // uint8_t data2[] = {0x00, 0x01}; // Write EXTCLK and SLEEP bit at same time according to datasheet instructions.
    // I2CWrite(address, data2, 2); // 
    uint8_t data3[] = {0xfe, 0x03}; // Set clock divier to 1526Hz 
    if (slow){ //In this case we slow down the clock divider so it PWMs at lower frequency since the switches have 1ms turn on time or such
        data3[1] = 0x1e; // Set the clock divider to 200 Hz. This is so we have less nonlineariy due to the slow turn-on time of the switches.
    } 

    I2CWrite(address, data3, 2); // 

    uint8_t data4[] = {0x00, 0x01}; // TUrn it back on (sleep bit to zero) with restart mode disabled
    I2CWrite(address, data4, 2); //

}



void setDACSPI(int channel, float value){
    // Used to set putput of the DAC AD5668 on switchboard.
    // first agument is which channel 1-8, or 0 which means set all channels at once.
    // second argument is the voltage from 0 to 3.3V
    // Note this requires the SPI bus to be active!

    if (value<0.0){
        value = 0.0;
    } else if (value>3.3){
        value = 3.3;
    }
    
    uint8_t address;// This is the address byte. It is the channel number.
    if (channel==0){ //This is the set-all-outputs case. See Table 10 of datasheet for addresses.
        address = 0x0F;
    } else {
        address = channel-1;
    }
    
    // Now we convert the value to a 16 bit number
    uint16_t value16 = static_cast<uint16_t>((value/3.3)*65535); //Converts the value to a number between 0 and 65535.
    // Break up the number into appropriate bytes for sending.
    uint8_t upperByte = (value16 >> 12) & 0x0F; // Shift right by 12 bits and mask out the lower 4 bits
    uint8_t middleByte = (value16 >> 4) & 0xFF; // Shift right by 4 bits and mask out the lower 8 bits
    uint8_t lowerByte = value16 & 0x0F; // Mask out the lower 4 bits
    
    uint8_t command = 0x03; // See table 9 of datasheet, this command means write to input register and update that channel. Note if we need better sync ebtween channels we could try the 0x02 mode with some timing so they all change at once.

    // We now have the data! We can cconstruct the message
    byte b0 = command; // Command byte
    byte b1 = address << 4 | upperByte; // Address byte and upper 4 bits of data
    byte b2 = middleByte; // Middle 8 bits of data
    byte b3 = lowerByte << 4; // Lower 4 bits of data
    
    // SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0)); // Note Mode 2 is needed for the SPI DAC. 
    digitalWriteFast(SPI_CS0, LOW); // Set chip-select for the DAC to low.
    SPI.transfer(b0); // Send MSB first
    SPI.transfer(b1);
    SPI.transfer(b2);
    SPI.transfer(b3); // Send LSB last
    digitalWriteFast(SPI_CS0, HIGH); // Set chip-select for the DAC to low.
    // SPI.endTransaction(); // End the transaction
    // RIght now the DAC should be set!

    // Now we will check all the above by printing out each bit
    // Serial.println("We sent command "+String(b0)+" address "+String(b1)+" upper "+String(b2)+" middle "+String(b3)+" lower ");

}

void setupDACSPI(bool turnOn = false){
    //This is for the AD5668 on the SyncBoard. It is a 16 bit DAC with 8 channels controlled by SPI.
    // first argument is if we want to turn it off or on. Default is off.

    //First set the control pins which at present we dont use, these are DD1/DD2 on the schematic.
    digitalWriteFast(DAC_LDAC_Bar, LOW); // Active low Load Dac pin. Default (unused) functionality is to be low. If we later want to use this to (for example) sync all the signals we need to control it dynamically.
    digitalWriteFast(DAC_CLR_Bar, HIGH); // Active low clear dac pin. Useful for clearing things if we do end up using LDAC feature too.

    //now we want to set up the DAC depending on how it is going to be used.
    // SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0)); // Note Mode 2 is needed for the SPI DAC. 

    
    if (turnOn){
            //bring it out of powerdown mode and do a poweron-reset.
        // First we will set the references int/ex folloing Table 11/12 of datasheet.
        byte b0 = 0x08; // Command for setting reference bits.
        byte b1 = 0x00; //Doesnt matter
        byte b2 = 0x00; //Doesnt matter
        byte b3 = 0x00; //Last bit tells it external reference. If you set 0x01 that is internal.
        digitalWriteFast(SPI_CS0, LOW); // Set chip-select for the DAC to low.
        SPI.transfer(b0); 
        SPI.transfer(b1);
        SPI.transfer(b2);
        SPI.transfer(b3);
        digitalWriteFast(SPI_CS0, HIGH); // Set chip-select for the DAC to high to disable comms. 

        // Now we set the clear code register so ifw e did any clearing it would set things to zero. In practice dont think we actuall yneed this.
        b0 = 0x05; // Command for setting clear code.
        b1 = 0x00; //Doesnt matter
        b2 = 0x00; //Doesnt matter
        b3 = 0x00; //0x00 means powers up to zero. 0x03 means "no operation" (table 15) but not sure what that actually means.
        digitalWriteFast(SPI_CS0, LOW); // Set chip-select for the DAC to low.
        SPI.transfer(b0);
        SPI.transfer(b1);
        SPI.transfer(b2);
        SPI.transfer(b3);
        digitalWriteFast(SPI_CS0, HIGH); // Set chip-select for the DAC to high to disable comms. 


        // Now we set the power mode register to turn things on. This is Table 14 of the datasheet.
        b0 = 0x04; // Command bytes indicate power mode change.
        b1 = 0x00; // Doesnt matter.
        b2 = 0x00; // 0 here means normaloperation (power on)
        b3 = 0xFF; // THis means we are setting all outputs to the on mode. 
        digitalWriteFast(SPI_CS0, LOW); // Set chip-select for the DAC to low.
        SPI.transfer(b0); // Send MSB first
        SPI.transfer(b1);
        SPI.transfer(b2);
        SPI.transfer(b3); // Send LSB last
        digitalWriteFast(SPI_CS0, HIGH); // Set chip-select for the DAC to high to disable comms. 

        // Now power on reset, not sure if this is actually needed given we just did a power on but yolo. Docs are bad on this, I am setting address to "all channels" but it is not clear if this is needed.
        b0 = 0x07; // Command bytes indicate power mode change.
        b1 = 0xF0; // Doesnt matter.
        b2 = 0x00; // Doesnt matter.
        b3 = 0x00; // Doesnt matter. 
        digitalWriteFast(SPI_CS0, LOW); // Set chip-select for the DAC to low.
        SPI.transfer(b0); // Send MSB first
        SPI.transfer(b1);
        SPI.transfer(b2);
        SPI.transfer(b3); // Send LSB last
        digitalWriteFast(SPI_CS0, HIGH); // Set chip-select for the DAC to high to disable comms. 

        // At this point it should be on, with externl refernce, and all channels at 0 volts.

    } else {//put it into powerdown mode.
        // Following is from Table 14 of the datasheet.
        byte b0 = 0x04; // Command bytes indicate power mode change.
        byte b1 = 0x00; //Address bit + dont cares are  zero in this mode.
        byte b2 = 0x02; // powerdown mode 10 means 10kOhm to ground.
        byte b3 = 0xFF; // THis means we are setting all outputs to the powerdown mode. Something to note is we COULD be doing this for one output at a time or even have them in different states, but we haven't done so.
        digitalWriteFast(SPI_CS0, LOW); // Set chip-select for the DAC to low.
        SPI.transfer(b0); // Send MSB first
        SPI.transfer(b1);
        SPI.transfer(b2);
        SPI.transfer(b3); // Send LSB last
        digitalWriteFast(SPI_CS0, HIGH); // Set chip-select for the DAC to high to disable comms. 

        // At this point it is powered down so new dac commands shouldnt do anything, and every channel should be 100k pull down to ground.
    }

    
    // SPI.endTransaction(); // End the transaction

}

// Not intended for public use. Use setLEDDACI2C or setMagDACI2C instead.
// Channel 1-8 and value 0-3.3V
void setDACI2C(int addr, int channel, float value){
    if (value<0.0){
        value = 0.0;
    } else if (value>3.3){
        value = 3.3;
    }

    uint8_t address;// This is the address byte. It is the channel number.
    if (channel==0){ //This is the set-all-outputs case. See Table 9 of datasheet for addresses.
        address = 0x0F;
    } else {
        address = channel-1;
    }

    uint8_t command = 0x30; //This is the command byte which means "write to input register and update that channel". See table 8 of datasheet.

    //Now we combine command and address to get the first byte of the message.
    uint8_t data1 = command | address; // This is the first byte of the message.

    uint16_t value16 = static_cast<uint16_t>((value/3.3)*65535); //Converts the value to a number between 0 and 65535.
    // Break up the number into appropriate bytes for sending.
    uint8_t upperByte = (value16 >> 8) & 0xFF; // upper 8 bits
    uint8_t lowerByte = value16 & 0xFF; // lower 8 bits

    //Now we combine the data1 and data bytes into a uint8_t array to send
    uint8_t data[3] = {data1, upperByte, lowerByte};
    I2CWrite(addr, data, 3); // Send the data to the DAC.

}

//Channel 1-8 and value 0-3.3V
void setLEDDACI2C(int channel, float value) {
    //This is to send valules to AD5669 on the LED board.
    if (LEDAttached == false){
        raiseError("You tried to set the DAC on the LED board but it is not attached. You should check your hardware and connections.");
        return;
    }
    setDACI2C(LED_DAC_ADR, channel, value);
}

//Channel 1-8 and value 0-3.3V
void setMagDACI2C(int channel, float value) {
    //This is to send valules to AD5669 on the Magnet board.
    if (MagAttached == false){
        raiseError("You tried to set the DAC on the Magnet board but it is not attached. You should check your hardware and connections.");
        return;
    }
    setDACI2C(MagBoard_DAC_ADR, channel, value);
}

void setupDACI2C(int addr, bool turnOn = false){

    //First we power everything down.
    uint8_t data1 = 0x40; // Command byte for set power mode.
    uint8_t data2 = 0x02; // Data byte for power down mode - this is 100kOhm to ground.
    uint8_t data3 = 0xFF; // Data byte for setting all channels to power down mode.
    uint8_t data[3] = {data1, data2, data3}; //Conbine into array.
    I2CWrite(addr, data, 3); // Send tp DAC

    //Now tell it to use external reference.
    data1 = 0x80; // Command for setting reference bits.
    data2 = 0x00; // Dont care
    data3 = 0x00; // Set internal referene to off. 0x01 would be on.
    data[0] = data1; data[1] = data2; data[2] = data3; //Combine into array.
    I2CWrite(addr, data, 3); // Send tp DAC

    if (turnOn){ //Only if we are turning it on do we pull it on.
        //Now we power it up.
        data1 = 0x40; // Command byte for set power mode.
        data2 = 0x00; // Data byte for power down mode - this is normal operation (i.e. it is on)
        data3 = 0xFF; // Data byte for setting all channels to on modee.
        data[0] = data1; data[1] = data2; data[2] = data3; //Combine into array.
        I2CWrite(addr, data, 3); // Send tp DAC
    }
}

void setupLEDDACI2C(bool turnOn = false){
    //This is to set up AD5669 on the LED board. It is a 16 bit DAC with 8 channels controlled by I2C.
    if (LEDAttached == false){
        raiseError("You tried to set up the DAC on the LED board but it is not attached. You should check your hardware and connections.");
        return;
    }
    setupDACI2C(LED_DAC_ADR, turnOn);
}

uint16_t readADCOnce(int channel, bool internal = false,  int ADC_ID = 0){
    
    uint8_t ADC_address;
    if (ADC_ID == 0){
        ADC_address = SyncBoard_ADC_ADR;
    } else if (ADC_ID == 1){
        ADC_address = LED_ADC_ADR;
    } else if (ADC_ID == 2){
        ADC_address = MagBoard_ADC_ADR;
    } else {
        raiseError("You tried to read from an ADC with ID "+String(ADC_ID)+" but that is not a valid ID. It should be 0, 1 or 2.");
        return 0;
    }
    
    uint8_t ADC_Command_LSB; //Declare this
    internal = false;  // haha I tricked you! I force this to false since as of 31/1/24 the PCB has the external reference soldered on. If you want it to be internal you would need to change the resistor jumper on the hardware before changing this in code.
    if (internal == true){
        ADC_Command_LSB = 0xC; // This is the command byte 4 LSB for the ADC. This is internal reference and ADC on.
    } else {
        ADC_Command_LSB = 0x4; // This is the command byte 4 LSB for the ADC. This is external reference and ADC on.
    }   
   
    uint8_t ADC_Command_MSB = 0x0; // This is the command byte 4 MSB for the ADC.
    if (channel == 0){ //Decode tthe channel numbers for ADS7828
        ADC_Command_MSB = 0x8;
    } else if (channel == 1){
        ADC_Command_MSB = 0xC;
    } else if (channel == 2){
        ADC_Command_MSB = 0x9;
    } else if (channel == 3){
        ADC_Command_MSB = 0xD;
    } else if (channel == 4){
        ADC_Command_MSB = 0xA;
    } else if (channel == 5){
        ADC_Command_MSB = 0xE;
    } else if (channel == 6){
        ADC_Command_MSB = 0xB;
    } else if (channel == 7){
        ADC_Command_MSB = 0xF;
    }
    // Now combine MSV and LSB
    uint8_t ADC_Command =(ADC_Command_MSB << 4) | ADC_Command_LSB;
    //Tell the ADC what channel to read from
    I2CWrite(ADC_address, ADC_Command);
    // Now we read two bytes one after another
    byte buffer[2]; //Create a buffer of two bytes that will store oure results
    I2CRead(ADC_address, buffer, 2); //Read two bytes from the ADC
    //Now combine the MSB and LSB into single 16 bit number
    uint16_t ADC_Value = (buffer[0] << 8) | buffer[1];

    // uint8_t ADC_Reading_MSB = I2CRead(SyncBoard_ADC_ADR);
    // uint8_t ADC_Reading_LSB = I2CRead(SyncBoard_ADC_ADR);
    // // Now we combine them into a 16 bit number
    // // uint16_t ADC_Value = (ADC_Reading_MSB << 8) | ADC_Reading_LSB;
    // // Serial.println("We got LSB "+String(ADC_Reading_LSB)+" and MSB "+String(ADC_Reading_MSB)+" and combined them to get "+String(ADC_Value)+" for channel "+String(channel)+" with command "+String(ADC_Command));

    return ADC_Value;


}

void setupPWMs(){
    //Function that does all the setup process for each IC in the system. 
    setupPWM(SyncBoard_PWM_Switches_ADR, true); // Setup the PWM chip that drives the switches, 
    setupPWM(SyncBoard_PWM_LVLShift_ADR); // Setup the PWM chip that drives the level shifters.
    if (LEDAttached){
        setupPWM(LED_PWM_ADR); // Setup the PWM chip that drives the LED channels.

    }

}

