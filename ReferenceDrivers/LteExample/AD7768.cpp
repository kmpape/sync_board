#include "AD7768.h"
#include "GlobalVariables.h"
#include "PinOut.h"
#include "SPI.h"
#include "Teensy_HID.h"

AD7768::AD7768() : chip_select_pin(0), dmaBufferPtr(nullptr) {
    teensy_device.sendError("Bad key provided for AD7768");
}

AD7768::AD7768(uint8_t identity) : chip_select_pin(identity){
    //Create a binary byte to store the mode (1 or 0) for each of the 8 channels. Should all start in 0
}

void AD7768::writeRegister(uint8_t regAddress, uint8_t value){
    delayNanoseconds(15); // minimum CS high time between commands is 10ns, so we set 15ns to be safe

    digitalWriteFast(chip_select_pin, LOW); // Set chip-select for the ADC to low.
    // delayNanoseconds(100); // small delay to allow CS to settle
    // R/W bit is bit 7 of REG address, 0 = write, 1 = read
    SPI.transfer(regAddress); //MSB means we write to device, then we are writing register
    SPI.transfer(value);

    // 16 bit SPI write time for SPI 4Mhz clock: 4us
    // Min CS low times for 1.2288MHz clock for senseboard ADC (modifier * tmclk):
    //      slow mode: 7.2 us || medium mode: 1.8us || fast mode: 0.9 us
    // So if set to slow mode (i.e. Mclck/32) an extra delay is needed as below:  (medium: Mclck/8 and fast: Mclck/4 are ok without extra delay)
    delayMicroseconds(5);

    digitalWriteFast(chip_select_pin, HIGH); // Set chip-select for the ADC to high to disable comms. 
}

uint8_t AD7768::readRegister(uint8_t regAddress){

    digitalWriteFast(chip_select_pin, LOW); // Set chip-select for the ADC to low.
    SPI.transfer(0x80 | regAddress); // MSB 1 means we are reading from device, then we are reading status register 0x09
    SPI.transfer(0x00); // send dummy byte to read data
    // delayMicroseconds(5);
    digitalWriteFast(chip_select_pin, HIGH); // Set chip-select for the ADC to high to disable comms. 

    delayNanoseconds(10); // small delay between CS high and low t29 on datasheet

    digitalWriteFast(chip_select_pin, LOW); // Set chip-select for the ADC to low.
    SPI.transfer(0x80 | 0x00); // send dummy command byte to read data
    uint8_t value = SPI.transfer(0x00); // get data from reg
    // delayMicroseconds(5);
    digitalWriteFast(chip_select_pin, HIGH); // Set chip-select for the ADC to high to disable comms. 

    return value;
}

void AD7768::softReset(){
    // First reset command
    writeRegister(AD7768_DATA_CONTROL_REG, 0x03);
    // Second reset command
    writeRegister(AD7768_DATA_CONTROL_REG, 0x02);

    // TODO add extra delay here based on low power mode and decimation rate and filter type
    delay(30); // approximation of startup time, cant imagine it would be much longer than this
    
}

void AD7768::syncSPI(){
    // First sync command
    writeRegister(AD7768_DATA_CONTROL_REG, 0x00);

    // Second sync command
    writeRegister(AD7768_DATA_CONTROL_REG, 0x80);

    // delay(60);
    // Full settling time of filter must elapse after a sync command before data is valid, but this can be monitored with the filter settled header bit
    // instead of coding in a manual delay that holds everything up
    // delay(X);
}

void AD7768::readStatusReg(){

    SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));

    uint8_t status = readRegister(AD7768_DEVICE_STATUS_REG);
    // Process status byte as needed

    // syncSPI();
    // syncSPI();

    SPI.endTransaction();

    teensy_device.sendInfo("AD7768 Status %X", status);

    
}

void AD7768::Initialise(volatile uint64_t* dmaBufferPtrIN){
    dmaBufferPtr = dmaBufferPtrIN;
    for (int i = 0; i < 8; i++){
        channelModes[i] = ChannelMode::MODE_A;
    } 
    
    // NOTE we do not mess with the ADC_Reset piun here since that is going to multiple chips so gets handled in enableSystem
    SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
    
    //First we will do a soft reset. We SHOULD have just done a hard reset with the enableSystem function so this is just to be sure.
    softReset();

    // Then we need to sync spi before we can start sending commands
    syncSPI();

    // Set channel standby state - will be set to this anyway by the reset
    writeRegister(AD7768_CHANNEL_STANDBY_REG, 0b00000000); // Enable all channels

    // Set power mode of ADC
    if (chip_select_pin == Senseboard_ADC_CS){
        
        // on machine 2
        // MCLK = 1.2288MHz
        // ODR = MCLK / (dec * mode)
        // ODR = 1.2288 MHz / (1024 * 8) = 150 SPS every 6.67 ms
        // ODR = 1.2288 MHz / (512 * 32) = 75 SPS every 13.3 ms
        // ODR = 1.2288 MHz / (1024 * 32) = 37.5 SPS every 26.7 ms
        // MCLK Period = 0.814us 
        // Settling time is ~50,000  MCLK periods for (1024 * 8) dec  = 40.7 ms
        // Settling time is ~100,000 MCLK periods for (512 * 32) dec  = 81.4 ms
        // Settling time is ~200,000 MCLK periods for (1024 * 32) dec = 162.8 ms

        // on machine 1
        // MCLK = 1.8432MHz
        // ODR = MCLK / (dec * mode)
        // ODR = 1.8432 MHz / (1024 * 8) = 225 SPS every 4.44 ms
        // ODR = 1.8432 MHz / (512 * 32) = 112.5 SPS every 8.89 ms
        // ODR = 1.8432 MHz / (1024 * 32) = 56.25 SPS every 17.78 ms
        // MCLK Period = 0.542us
        // Settling time is ~50,000  MCLK periods for (1024 * 8) dec  = 27.1 ms
        // Settling time is ~100,000 MCLK periods for (512 * 32) dec  = 54.2 ms
        // Settling time is ~200,000 MCLK periods for (1024 * 32) dec = 108.4 ms
        // Settling time is ~6700 MCLK periods for (32 * 32) dec = 3.6 ms

        // This data output from the ADC every ODR seconds but there will only be NEW data every settling time seconds for each channel

        //Set Channel readout Mode A - the mode we will use for fast/low latency/high noise readout
        writeRegister(AD7768_CHANNEL_A_CONFIG_REG, 0b00001100); // Sinc5 filter with x512 decimation. Output data rate with MCLK=32 should be clock/(512*32)
        // writeRegister(AD7768_CHANNEL_A_CONFIG_REG, 0b10001111); // Sinc5 filter with x1024 decimation. 

        //Set Channel readout Mode B - SINC5 filter with a smaller window, i.e. less decimation for faster response, could be used for evacuate to detector
        writeRegister(AD7768_CHANNEL_B_CONFIG_REG, 0b00001000); // Sinc5 with x32 decimation. Output data rate with MCLK=32 should be clock/(32*32)

        // Set power mode to Eco mode for Senseboard ADC
        writeRegister(AD7768_POWER_MODE_REG, 0b00000000); // Sleep mode disabled / Eco mode/LVDS input clock disabled / MCLK Div 32 for lowest output rate
        // writeRegister(AD7768_POWER_MODE_REG, 0b00100010); // Sleep mode disabled / Medium mode/LVDS input clock disabled / MCLK Div 8 for higher output rate which we can then up the decimation
        // writeRegister(AD7768_POWER_MODE_REG, 0b00110011); // Sleep mode disabled / Fast mode/LVDS input clock disabled / MCLK Div 4 for highest output rate which we can then up the decimation
        
        // according to page 44 revD, they recommend power modes for certain Fmod frequencies. Since Mclk is 1.2288 MHz and lowest divider is 4, max fmod is 0.3 Mhz. This falls into the low power range of Table 11.
        // BUT it could just scale and so in that case if divider 4 is used then is should acutally be fast mode. So maybe we should try that. 0b00110011
        // Could also be worth bypassing the divider chip we have to get an Mclk input of 4.9152 MHz

    } else if (chip_select_pin == Topboard_ADC_CS){
        //Set Channel readout Mode A - the mode we will use for fast/low latency/high noise readout
        writeRegister(AD7768_CHANNEL_A_CONFIG_REG, 0b00001100); // Sinc5 filter with x512 decimation. Output data rate with MCLK=32 should be clock/(512*32)
        
        //Set Channel readout Mode B - the mode we will use for low noise OD readings
        writeRegister(AD7768_CHANNEL_B_CONFIG_REG, 0b00000111); // Wideband filter with x1024 decimation. Output data rate with MCLK=32 should be clock/(1024*32)
        
        // Set power mode to Fast mode for Topboard ADC
        writeRegister(AD7768_POWER_MODE_REG, 0b00110000); // Sleep mode disabled / fast mode /LVDS input clock disabled / MCLK Div 32 for most averaging possible
    }        

    // Set general config of device - not default
    writeRegister(AD7768_GENERAL_CONFIG_REG, 0b01011000); // bit 6 = 1 disable external clock validation,  bit 4 = 1 to disable VCM buffer as we dont use VCM, Bit 3 to 1 because the datasheet somehow claimed this was the reset/standard config here (i.e. to give 0x08) though it is "reserved"

    // Set data control setup for device - will be set to this anyway by the reset
    writeRegister(AD7768_INTERFACE_CONFIG_REG, 0b00000000);  // bit 3,2 are saying no CRC, Bit 1,0 are saying DCLK divide by 8 (i.e. as slow as possible)
        
    //Set up GPIO modes for the GPIO pins default is disabled and all inputs
    /*
    1 << 7 // GPIO ENABLE
    0 << 4 // GPIO4 UNUSED set as output to prevent it from being floating
    1 << 3 // GPIO3 OUTPUT
    1 << 2 // GPIO2 OUTPUT
    1 << 1 // GPIO1 OUTPUT
    1 << 0 // GPIO0 OUTPUT
    */ 
    writeRegister(AD7768_GPIO_CONTROL_REG, 0b10011111); 

    // Set all GPIO outputs low initially
    writeRegister(AD7768_GPIO_WRITE_REG, 0b00000000); 
    
    //At this point we could set the input precharge buffers at addresses 0x11 and 0x12 but they should be OK by default (all on!)
    //We also set 0x13/14 the buffers for the reference inputs to ON, as they are OFF by default. At the moment without these we are drawing something like 72 uA/V/Channel i.e. 2mA or so. With buffers we could reduce this to 500uA which seems a bit better, hence we will do so.
    writeRegister(AD7768_POSITIVE_REF_BUFFER_REG, 0b11111111); // All channel reference buffers to on. NOTE the datasheet (pg 81) seems to imply we just write this unlike some weird inverse thing for the input buffers. Hopefully this is correct lol
    writeRegister(AD7768_NEGATIVE_REF_BUFFER_REG, 0b11111111); // All channel reference buffers to on. NOTE the datasheet (pg 81) seems to imply we just write this unlike some weird inverse thing for the input buffers. Hopefully this is correct lol

    syncSPI();

    SPI.endTransaction();
}

uint32_t* AD7768::getADCBits(){
    static uint32_t raw_bits[8];
    for (int i=0; i<4; i++){
        raw_bits[i*2] = uint32_t(dmaBufferPtr[i]);
        raw_bits[(i*2)+1] = uint32_t(dmaBufferPtr[i]>>32);
    }
    return raw_bits;
}

uint32_t AD7768::getADCInt(uint8_t channel_ID){
    uint32_t raw_bits = uint32_t(dmaBufferPtr[channel_ID/2]>>(32*(channel_ID%2))) & 0x00FFFFFF;
    if ((raw_bits >> 23) & 0b1){ // if the sign bit is set
        raw_bits = raw_bits | 0xFF000000; // extend the sign bit
    }
    int32_t output_integer = static_cast<int32_t>(raw_bits);
    return output_integer;
}

AD7768Data* AD7768::getADCData(){
    static AD7768Data data[8];
    uint32_t raw_bits;
    uint32_t raw_data_bits;
    for (int i=0; i<8; i++){
        // because dividing an int by 2 will always round down:
        // i = 0 and 1, dmaBufferPtr[0] 
        // i = 2 and 3, dmaBufferPtr[1]
        // i = 4 and 5, dmaBufferPtr[2]
        // i = 6 and 7, dmaBufferPtr[3]
        // the 32*(i%2) will be 0 for i=0,1,2,3 and 32 for i=4,5,6,7, so we can use this to get the correct 32 bits within a 64 bit integer
        raw_bits = uint32_t(dmaBufferPtr[i/2]>>(32*(i%2))); 
        data[i].error_flag              = (raw_bits >> 31) & 0b1;
        data[i].filter_not_settled      = (raw_bits >> 30) & 0b1;
        data[i].repeated_data           = (raw_bits >> 29) & 0b1;
        data[i].filter_type             = (raw_bits >> 28) & 0b1; // SINC5 [1] or Wideband [0]
        data[i].filter_saturated        = (raw_bits >> 27) & 0b1;
        data[i].channel_ID              = (raw_bits >> 24) & 0b00000111;
        raw_data_bits = raw_bits & 0x00FFFFFF;
        if ((raw_data_bits >> 23) & 0b1){ // if the sign bit is set
            raw_data_bits = raw_data_bits | 0xFF000000; // extend the sign bit
        }
        data[i].value = static_cast<int32_t>(raw_data_bits);
    }
    return data;
}


void AD7768::SetChannelMode(int channel, ChannelMode mode){
    // Used to set the channel mode of a channel then write to the ADC the new 
    // channel mode. If channel is -1 then we set all channels to the mode.
    //The reason we have a private variable for this is that we have a constant state stored for the ADC so that if you just want to change one channel you dont need to read the ADC first.
    
    // Note that 
    //  mode=0 corresponds to channel type A which is the fast response/SINC5 filter
    //  mode=1 corresponds to channel type B which is the low noise/Wideband filter for doing OD readings.

    // teensy_device.sendInfo("Channel mode before: %d %d %d %d %d %d %d %d", channelModes[7], channelModes[6], channelModes[5], channelModes[4], channelModes[3], channelModes[2], channelModes[1], channelModes[0]);
    //First check if channel is valid 0-7 inclusive or -1 for all
    if (channel >= 0 && channel < 8){
        //In this case we will change the corresponding bit of channelModes to whatever is sent in on mode
        channelModes[channel] = mode; //Note we are labelling channels/.reactors from 1-8 but here need bit indexing 0 to 7
    } else if (channel==-1) {
        //In this case we want to set every bit of channelmodes to the specified value, used in setup!
        for (int i = 0; i < 8; i++){
            channelModes[i] = mode;
        }
    }
    // teensy_device.sendInfo("Channel mode after: %d %d %d %d %d %d %d %d", channelModes[7], channelModes[6], channelModes[5], channelModes[4], channelModes[3], channelModes[2], channelModes[1], channelModes[0]);
    
    //Now convert channelModes to a single byte
    uint8_t channelModeByte = 0;
    for (int i = 0; i < 8; i++){
        channelModeByte = channelModeByte | (bool(channelModes[i]) << i);
    }
    teensy_device.sendDebug("Sending Byte = %d Channel:%d Mode:%d", channelModeByte, channel, mode);

    SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
    //Now we write this to the ADC
    writeRegister(AD7768_CHANNEL_MODE_SELECT_REG, channelModeByte);
    // TODO test if changing the channel mode corrupts data on od readings because of this sync call
    syncSPI();
    SPI.endTransaction();

}

void AD7768::SetGPIOMode(uint8_t state){
    // This sets up the GPIO from the SyncBoard ADC to which is actuall.y configuring the OTHER RTD ADC. A bit of messy circuitry but necessary as we didnt have enough other digital outputs to do this
    // In practice state should be between 1111 and 0000, anything else wont be valid so we check this first
    // this gets run periodically throughout the programme as the other ADC is multiplex switching the RTD inputs
    //Note the definition of state is:
    // 0,0,0,0,RTD_Polarity,MX3,MX2,MX1

    // TODO this causes an error on channels 1,3,5,7 on the ad7768 when run repeatedly - need to investigate why
    // if (state > 0x0F){
    //     teensy_device.sendError("Invalid AD7768 GPIO pin state. It should be between 0x0 and 0xF");
    //     return;
    // } 
    // teensy_device.sendInfo("Setting AD7768 GPIO to state: %d", state);

    SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
    // 0x0F REG - writes the value to be used on each pin when set to output
    /*
    0 << 4 // GPIO4 UNUSED
    1 << 3 // GPIO3 HIGH/LOW 1/0
    1 << 2 // GPIO2 HIGH/LOW 1/0
    1 << 1 // GPIO1 HIGH/LOW 1/0
    1 << 0 // GPIO0 HIGH/LOW 1/0
    */ 
    writeRegister(AD7768_GPIO_WRITE_REG, state);
    // syncSPI();
    SPI.endTransaction();
}

void AD7768::StartADC(){
    // COmmand that must be run after changing config of things to make it read properly.
    SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
    // Last command now is SPI_SYNC where we tell it to start reading stuff out. Note this is stated as necessary on pg50/51 of datasheet
    syncSPI();
    //Now it should start reading data on all enabled channels! Data should be coming out of DOUT0
    SPI.endTransaction();

}
