#include "AD5675.h"
#include "Teensy_HID.h"
//Note I found good temp driver for this here https://github.com/analogdevicesinc/linux/blob/main/drivers/media/i2c/ov5675.c
AD5675::AD5675() : i2caddr(0){
    teensy_device.sendError("Bad key provided for AD5675");
}

AD5675::AD5675(uint8_t addr) : i2caddr(addr){
}

void AD5675::Initialise(){
    // Initialise the DAC
    // THIS should turn everythhing to the default safe/startup state
    Wire1.beginTransmission(i2caddr);
    Wire1.write(B01000000);  // Write control register to power up/down mode, i believe dac adddress nto important here
    Wire1.write(B00000100);  // Power up dac 7,6,4 and put dac5 in pulldown to ground mode
    Wire1.write(B00000000);  // Power up dac 3,2,1,0
    uint8_t result0 = Wire1.endTransmission();
    
   
    // delay(10); 
    //Now we will do a sooftware reset, we dont really think this is necessary. It should set all outputs to 0V since the rstsel pin on device is grounded
    Wire1.beginTransmission(i2caddr);
    // // Wire1.write(B00000000);  // Write control register to software reset -NOTE I COULDNT GET THIS TO WORK somehow it always gave errors
    // // Wire1.write(0x12); //This is a bit arbitrary and specified on page 29 of the datasheet.
    // // Wire1.write(0x34);
    Wire1.write(B10110000);  // Write to all channels at once to set to 0
    Wire1.write(B00000000); //just 0
    Wire1.write(B00000000); //also 0

    //End the transmission
    uint8_t result2 = Wire1.endTransmission();
    // delay(10); 
    // uint8_t result = Wire1.endTransmission();
    // Check for acknowledgment or errors

    // Serial.println(result1);
    // Serial.print("Result 2: ");
    // Serial.println(result2);

    
    if (result0 == 0 && result2 == 0)
    {

    }
    else
    {
        teensy_device.sendError("Setup of AD5675 failed, Error codes: %d, %d", result0, result2);
    }

    
}

uint16_t AD5675::GenerateRef(float voltage) {
  // Description:
  // voltage is the voltage to convert to a reference number
  // returns the reference number to set the voltage, it is an integer between 0-4096 and represents a voltage between 0-5V
    if (voltage > AD5675_MaxVoltage)
    {
        voltage = AD5675_MaxVoltage;
    }
    else if (voltage < 0)
    {
        voltage = 0;
    }
    // 0-5V = 0-65535
    // 5-2 / 5 = 65535 - x / 65535
    // x = 65535 - ((65535 * (DAC_MaxVoltage - voltage)) / DAC_MaxVoltage)
    return (uint16_t)(65535 - ((65535 * (AD5675_MaxVoltage - voltage)) / AD5675_MaxVoltage));
}

void AD5675::SetVoltage(uint8_t channel, float voltage) {
    // Description:
    // channel is the channel number on the DAC
    // voltage is the requested voltage in V to set the DAC channel to

    uint16_t ref = GenerateRef(voltage);
    //The reference number is a 16 bit number, we need to split it into two 8 bit numbers
    //First get the most significant byte
    uint8_t ref_msb = (ref & 0xFF00) >> 8;
    //Then get the least significant byte
    uint8_t ref_lsb = ref & 0x00FF;
    //Now we make the command address which should be four bits where 0000 means dac0 through to 0111 meaing dac7
    //We also append 0011 to the front of this to make the command address
    uint8_t command = (REG_WriteVoltage << 4) | channel;
    
    Wire1.beginTransmission(i2caddr);
    Wire1.write(command);
    Wire1.write(ref_msb);
    Wire1.write(ref_lsb);

    // End the transmission
    uint8_t result = Wire1.endTransmission();
    // Check for acknowledgment or errors
    if (result != 0)
    {
        teensy_device.sendError("Senseboard DAC I2C failed: %d", result);
    }
}