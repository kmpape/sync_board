#include "ADS124S08.h"
#include "GlobalVariables.h"
#include "PinOut.h"
#include "SPI.h"
#include "Teensy_HID.h"

// NOTE this is a bit of a messy driver in that it is very design-dependent and not a generic driver for the chip at all. But yolo.




ADS124S08::ADS124S08() {
    teensy_device.sendError("Bad key provided for ADS124S08");
    // Anything we need to initialise could go here

}


void ADS124S08::readData(){
    //SOmething happens
   
}

void ADS124S08::debugBuffer(){
  //Some more debugging stuff
}

void ADS124S08::writeRegister1Byte(uint8_t regAddress, uint8_t value){

    digitalWriteFast(Senseboard_ADC2_CS, LOW); // Set chip-select for the ADC to low.
    SPI.transfer(0b01000000 | regAddress); //MSB means we write to device, then we are writing register 
    SPI.transfer(0x00); // Tell it you want to write one byte (note this number is num_bytes_to_write-1)
    SPI.transfer(value); 
    digitalWriteFast(Senseboard_ADC2_CS, HIGH); // Set chip-select for the ADC to high to disable comms. 

}

uint8_t ADS124S08::readRegister1Byte(uint8_t regAddress){

    digitalWriteFast(Senseboard_ADC2_CS, LOW); // Set chip-select for the ADC to low.
    SPI.transfer(0b00100000 | regAddress); //MSB means we read from device
    SPI.transfer(0x00); // Tell it you want to read one byte (note this number is num_bytes_to_write-1)
    uint8_t value = SPI.transfer(0x00); 
    digitalWriteFast(Senseboard_ADC2_CS, HIGH); // Set chip-select for the ADC to high to disable comms. 

    return value;
}

void ADS124S08::sendCommand(uint8_t command){
    digitalWriteFast(Senseboard_ADC2_CS, LOW); // Set chip-select for the DAC to low.
    SPI.transfer(command); //
    digitalWriteFast(Senseboard_ADC2_CS, HIGH);
}

void ADS124S08::Initialise(){
   
    // We start setup here. Broadly there are two categories of commands, single-commands in Table 24 of datasaheet, then the RREG/WREG commands which involve the regfs in table 25
    SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE1));

    // First we tell it to wakeup, probably not necessary but a good place to start
    sendCommand(ADS124S08_CMD_WAKEUP); //Wakeup command table24

    //Now send a reset, again to ensure we have a blank slate
    sendCommand(ADS124S08_CMD_RESET); //Reset command table24

    delay(ADS124S08_RESET_DELAY_MS); // Wait for it to reset 

    // Now clear the power on reset field in status reguster 0x01, note this is specified as being necessary on pg 53/section 9.3.10.1
    writeRegister1Byte(ADS124S08_STATUS_REG, 0x00); // Bit 7 iimportantly 0, all the others are don't care (i.e. this is read only)

    SPI.endTransaction();
    // Everything else bwlow is pretty much case specific hence we dewal wityh it in configure and multplexer functions.


}
void ADS124S08::startConversion(){
    // Send START command to tell it to start converting.
    SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE1));

    sendCommand(ADS124S08_CMD_START); //Start conversions command Table 24

    SPI.endTransaction();
}

byte ADS124S08::getStatus(){
    // This is a trash hacking function, generally dont uise

    SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE1));


    // digitalWriteFast(Senseboard_ADC2_CS, LOW); // Set chip-select for the DAC to low.
    // SPI.transfer(B00100001); //Bit 5 is RREG, and we want to read register 1. 
    // SPI.transfer(B00000000); // Tell it you want to read one byte (note this number is num_bytes_to_write-1)
    // byte byte1 = SPI.transfer(0x00); //Read first byte
    // digitalWriteFast(Senseboard_ADC2_CS, HIGH);

    uint8_t byte1 = readRegister1Byte(ADS124S08_STATUS_REG);

    // digitalWriteFast(Senseboard_ADC2_CS, LOW); // Set chip-select for the DAC to low.
    // dummy = SPI.transfer(B00100000); //Bit 5 is RREG, and we want to read register 0. 
    // dummy = SPI.transfer(B00000000); // Tell it you want to read one byte (note this number is num_bytes_to_write-1)
    // byte byte1b = SPI.transfer(0x00); //Read first byte
    // digitalWriteFast(Senseboard_ADC2_CS, HIGH);


    //     digitalWriteFast(Senseboard_ADC2_CS, LOW); // Set chip-select for the DAC to low.

    //     dummy = SPI.transfer(B00100110); //Bit 5 is RREG, and we want to read register 1. 

    //     dummy = SPI.transfer(B00000000); // Tell it you want to read one byte (note this number is num_bytes_to_write-1)

    //     byte byte2 = SPI.transfer(0x00); //Read first byte
    //     digitalWriteFast(Senseboard_ADC2_CS, HIGH);
        
    //     digitalWriteFast(Senseboard_ADC2_CS, LOW); // Set chip-select for the DAC to low.

    //     dummy = SPI.transfer(B00100111); //Bit 5 is RREG, and we want to read register 1. 

    //     dummy = SPI.transfer(B00000000); // Tell it you want to read one byte (note this number is num_bytes_to_write-1)

    //     byte byte3 = SPI.transfer(0x00); //Read first byte
    //     digitalWriteFast(Senseboard_ADC2_CS, HIGH);

    SPI.endTransaction();

    return byte1;

}

void ADS124S08::configure(ADS124S08_MODE mode){
    // mode 1 is for RTD mode, mode 2 is for LED current mode, mode 3 is for pressure1, mode4 is pressure2, mode 5-8 are [calibration modes]
    SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE1));

    //First we set up the gain modes
    if (mode == ADS124S08_MODE::RTD || mode == ADS124S08_MODE::GAIN_CAL_1 || mode == ADS124S08_MODE::GAIN_CAL_2 || mode == ADS124S08_MODE::GAIN_CAL_3 || mode == ADS124S08_MODE::OFFSET_CAL ){ //These are ones with gain = 8
        writeRegister1Byte(ADS124S08_GAIN_REG, 0b11001011); // Bit 3 means PGA enabled and bigs 0-2 imply gain=8. Bits 7,6 set delay to 4096*tmod with tmod=tclk*16 which should mean approx 16ms to give input filter time to settle.

    } else if (mode == ADS124S08_MODE::LED_CURRENT){
        writeRegister1Byte(ADS124S08_GAIN_REG, 0b01101011); // Bit 3 means PGA enabled and bits 0-2 imply gain=8. Bits 7,6 set delay to 256*tmod so should be approx 1ms,

    } else if (mode == ADS124S08_MODE::PRESSURE_1 || mode == ADS124S08_MODE::PRESSURE_2){ //These are ones with gain = 1 which are the pressure sensors
        writeRegister1Byte(ADS124S08_GAIN_REG, 0b01100000); // Bit 3 means PGA disabled and bits 0-2 imply gain=1 though they dont do anything according to Table29. Bits 7,6 put delay at 256*tmod so should be approx 1ms.
    } 

    //Next we do the DATARATE stuff
    if (mode == ADS124S08_MODE::RTD ){ //This is standard RTD mode
        // This is single shot, global chop enabled, 20SPS, Low Latency filter, which should take 113ms to settle (Table18). We picked this since it blocks 50/60Hz and has low noise and we are wanting to chop it 4 or 6 times hsnce this would take us up to approx 1 second
        writeRegister1Byte(ADS124S08_DATARATE_REG, 0b10110100); 
        
    } else if (mode == ADS124S08_MODE::LED_CURRENT || mode == ADS124S08_MODE::PRESSURE_1 || mode == ADS124S08_MODE::PRESSURE_2 ){ // LED current mode and pressure sensors
        // This is single shot, Global chop disabled, low latency filter, 50sps which has settlign time of approx 20ms (Table 13). Basicallyt we just choose something fast
        writeRegister1Byte(ADS124S08_DATARATE_REG, 0b00110101); 
        
    } else if ( mode == ADS124S08_MODE::GAIN_CAL_1 || mode == ADS124S08_MODE::GAIN_CAL_2 || mode == ADS124S08_MODE::GAIN_CAL_3){
        // This is global chop, internal clock, SINGLE SHOT, low latency filter, 5SPS to do some nice integrating (ie. it will take at least 400ms to setttle...)
        writeRegister1Byte(ADS124S08_DATARATE_REG, 0b10110001); 
        
    } else if (mode == ADS124S08_MODE::OFFSET_CAL){
        // This is global chop, internal clock, CONTINUOUS CONVERSION (bit unclear but think this might be needed for SYOCAL), low latency filter, 5SPS to do some nice integrating (ie. it will take at least 400ms to setttle...)
        writeRegister1Byte(ADS124S08_DATARATE_REG, 0b10010001); 
    }

    // Set up voltage references.
    if (mode == ADS124S08_MODE::RTD || mode == ADS124S08_MODE::LED_CURRENT || mode == ADS124S08_MODE::GAIN_CAL_1 || mode == ADS124S08_MODE::GAIN_CAL_2 || mode == ADS124S08_MODE::GAIN_CAL_3 || mode == ADS124S08_MODE::OFFSET_CAL ){ //This is standard RTD mode or the LED current mode and all calibration modes
        // Enable both reference buffers, attach to REFP0 REFN0, use external reference BUT POWER ON INTERNAL REFERENCE since IDAC needs this (bit of a niche fact which took a while to work out, see datasheet pg36)
        writeRegister1Byte(ADS124S08_REF_REG, 0b00000001); 

    } else if (mode == ADS124S08_MODE::PRESSURE_1 || mode == ADS124S08_MODE::PRESSURE_2){ //  pressure sensors - need to use internal reference
        // Disable reference buffers and enable internal reference and turn it on.
        writeRegister1Byte(ADS124S08_REF_REG, 0b00111001); 
    } 
    
    //Now the excitation current setup.
    //This is standard RTD mode, and all gain calibration modes since they require current going through the system. And current sensor as it also desires the samer eference to work
    if (mode == ADS124S08_MODE::RTD || mode == ADS124S08_MODE::LED_CURRENT || mode == ADS124S08_MODE::GAIN_CAL_1 || mode == ADS124S08_MODE::GAIN_CAL_2 || mode == ADS124S08_MODE::GAIN_CAL_3 || mode == ADS124S08_MODE::OFFSET_CAL ){ 
        // enable current source at 250uA
        writeRegister1Byte(ADS124S08_IDAC1_REG, 0b00000100);

        // connect IDAC1 to AIn6
        writeRegister1Byte(ADS124S08_IDAC_MUX_REG, 0b11110110); 

    } else if ( mode == ADS124S08_MODE::PRESSURE_1 || mode == ADS124S08_MODE::PRESSURE_2 ){ //  pressure sensors - need to use internal reference
        // Disable current source.
        writeRegister1Byte(ADS124S08_IDAC1_REG, 0b00000000); 

        // Disconnect IDAC1
        writeRegister1Byte(ADS124S08_IDAC_MUX_REG, 0b11111111); 
    } 

    // NOTE at this point there are various system config things we COULD do in register 0x09 but I think we are good for now.
    SPI.endTransaction();
}

void ADS124S08::setMultiplexer(ADS124S08_MODE mode){
    // mode 1 is for RTD mode, mode 2 is for LED current mode, mode 3 is for pressure1, mode4 is pressure2, mode 5 is gain calibration mode 1, mode 6 gain calibration mode 2, mode 7 gain calibration mode 3
    //NOTE these modes should be same as the configure modes above.
    SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE1));

    if (mode == ADS124S08_MODE::RTD ){ //This is standard RTD mode
        writeRegister1Byte(ADS124S08_MUX_REG, 0b00100011); // This means MUXP = AIN2, MUXN = AIN3

    } else if (mode == ADS124S08_MODE::LED_CURRENT ){ // LED current mode
        writeRegister1Byte(ADS124S08_MUX_REG, 0b01000101); // This means MUXP = AIN4, MUXN = AIN5

    } else if (mode == ADS124S08_MODE::PRESSURE_1 ){ // Pressure 1
        writeRegister1Byte(ADS124S08_MUX_REG, 0b10101100); // This means MUXP = AIN10 and MUXN is AINCOM (ground)

    } else if (mode == ADS124S08_MODE::PRESSURE_2 ){ // Pressure 2
        writeRegister1Byte(ADS124S08_MUX_REG, 0b10111100); // This means MUXP = AIN11 and MUXN is AINCOM (ground)

    } else if (mode == ADS124S08_MODE::GAIN_CAL_1 ){ // Gain calibration mode 1 - meant to give a readout of 16/22 of FSR
        writeRegister1Byte(ADS124S08_MUX_REG, 0b01111000); // This means MUXP = AIN7 = VsenseP and MUXN = AIN8 = VGL

    } else if (mode == ADS124S08_MODE::GAIN_CAL_2 ){ // Gain calibration mode 2 - meant to give a readout of 8/22 of FSR
        writeRegister1Byte(ADS124S08_MUX_REG, 0b01111001); // This means MUXP = AIN7 = VsenseP and MUXN = AIN9 = VGM

    } else if (mode == ADS124S08_MODE::GAIN_CAL_3 ){ // Gain calibration mode 3 - meant to give a readout of 8/22 of FSR - I THINK this one shoudl be the most precise since the two nputs are most balanced...
        writeRegister1Byte(ADS124S08_MUX_REG, 0b10011000); // This means MUXP = AIN9 = VGM and MUXN = AIN8 = VGL

    } else if (mode == ADS124S08_MODE::OFFSET_CAL ){ // Offset calibration mode - connects VADCMID on AIN0/1
        writeRegister1Byte(ADS124S08_MUX_REG, 0b00000001); // This means MUXP = AIN0 and MUXN = AIN1
    } 

    SPI.endTransaction();
}

float ADS124S08::getLastRTD(){
    // Returns final RTD Reading from whatever was last cycle.
    // teensy_device.sendInfo("RTD Reading final raw is %.2f", RTDReadingFinal);
    float RTDReadingFinalCalibrated = RTDReadingFinal * gainCalibrationFactor /8.0; // Apply the gain calibration factor to the RTD reading and undo our gaining.
    return RTDReadingFinalCalibrated;
}

int32_t ADS124S08::readADCRaw(){
    //First we send the RDATA command
    SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE1));

    digitalWriteFast(Senseboard_ADC2_CS, LOW); // Set chip-select for the DAC to low.
    SPI.transfer(ADS124S08_CMD_RDATA); //RDATA command from Table 24 - this tells the device we are about to read data
    // Now I want to read a byte from the device - note it comes back in twos complement

    int32_t raw_data = 0;
    
    byte byte1 = SPI.transfer(0x00); //Read first byte
    byte byte2 = SPI.transfer(0x00); //Read second byte
    byte byte3 = SPI.transfer(0x00); //Read third byte
    
    digitalWriteFast(Senseboard_ADC2_CS, HIGH);

    SPI.endTransaction();

    if (byte1 & 0x80){
        //This is a negative number
        raw_data = 0xFF000000 | (byte1 << 16) | (byte2 << 8) | byte3;
    } else {
        //This is a positive number
        raw_data = (byte1 << 16) | (byte2 << 8) | byte3;
    }

    return raw_data;
}


float ADS124S08::readADC(){
    int32_t raw_data = readADCRaw(); // Reads raw data from the ADC.
    // teensy_device.sendInfo("Read ADC value %d", raw_data);
    float value = (float)raw_data / 8388608.0; // second number is 2^23 as DAC is 24 bit and 1 bit is sign.
    //NOTE this is a somewhat arbitrary number between -1 and 1 that we need to make sense of elserqwhere.
    return value;
}


int ADS124S08::readRTD(int RTDIndex, int RTDStep){
    // Used to step through RTD readings on RTDIndex RTD.
    if (RTDStep==0){
        RTDReadingSum = 0.0; // Note that this wouldn't be necessary since it is above UNLESS we had been interrupted by one of the toher ADC fastreads and hence this might not start at 0
    } else {
        float value = readADC();
        // If RTDStep is an odd number we need to multiply value by negative 1 as we will have flipped orientation of connection to the ADC with our offset mitigator circuit.
        if (RTDStep % 2 == 0){
            value = value * -1.0;
        }
        RTDReadingSum += value;

        // teensy_device.sendInfo("Read ADC value was %.7f and new RTDReadingSum is %.7f", value, RTDReadingSum); // Just lookign at this for now...!

    }
    

    if (RTDStep == maxRTDReadings){ //In this case we are done.
        RTDStep=-1; //-1 indicates it is finished, this is picked up inside machine::runRTDADC
        RTDReadingFinal = RTDReadingSum/ (float) maxRTDReadings; //Calculate the final RTD reading as average over the readings we have taken.
        // teensy_device.sendInfo("Final value for RTD number %d is %.7f", RTDIndex, RTDReadingFinal);
        RTDReadingSum = 0.0; //Reset the running sum.
    } else {
        RTDStep+=1; // Increment RTDStep

    }
    
    return RTDStep; //Send back value to be handled in other function.
}


int ADS124S08::calibrate(int calibrationStep){
    // This does the internal commands in the device that allow it to self calbirate with SYOCAL/SYGCAL - Note we dont do SFOCAL since it seems like bad variant of SYOCAL.
    // teensy_device.sendInfo("Entered calibration with step = %d at which point status is %d", calibrationStep, getStatus());

    if (calibrationStep == 0){
        // Do SYOCAL - must connect the inputs to AIN0/1 and then run command.
        // Note this must be run in continuous conversion mode! 
        configure(ADS124S08_MODE::OFFSET_CAL); // Set up for SYOCAL
        setMultiplexer(ADS124S08_MODE::OFFSET_CAL); // Set up for SYOCAL
        startConversion(); // Start the conversion - TECHNICALLY this isnt needed since this configuration is in continuous conversion mode, but why not  - according to datasheet it should do nothing if you are already converting.
        // At this point it is meant to do the SYOCAL automatically i.e. it will take the readings and update the offset register in the device. 
        // Note that nothing happens now for the time set out in RTDADC_calibrationTimeout
        calibrationStep = 1;
    } else if (calibrationStep == 1){
        // We dont need to do any cleanup here since SYOCAL is done internally in the device. 
        // Next we go to the gain calibration step 1
        configure(ADS124S08_MODE::GAIN_CAL_1);
        setMultiplexer(ADS124S08_MODE::GAIN_CAL_1);
        startConversion();
        // Gain calibration step 1 - VSenseP to VGL
        calibrationStep = 2;
    } else if (calibrationStep == 2){
        // We need to clean up the gain calibration step 1
        gainCalibration1 = readADC(); // Read the value from the ADC - it should be ready at this point since above mode runs single shot.
        // Gain calibration step 2 - VSenseP to VGM
        configure(ADS124S08_MODE::GAIN_CAL_2);
        setMultiplexer(ADS124S08_MODE::GAIN_CAL_2);
        startConversion();

        calibrationStep = 3;
    } else if (calibrationStep == 3){
        // Gain calibration step 3 - VGM to VGL
        gainCalibration2 = readADC(); // Read the value from the ADC - it should be ready at this point since above mode runs single shot.
        configure(ADS124S08_MODE::GAIN_CAL_3);
        setMultiplexer(ADS124S08_MODE::GAIN_CAL_3);
        startConversion();
        calibrationStep = 4;
    } else if (calibrationStep == 4){
        // Final cleaning up
        gainCalibration3 = readADC(); // Read the value from the ADC - it should be ready at this point since above mode runs single shot.
        calibrationStep  = 5;
        // Now lets print the three gain calibration factors and see what they look like

        // Now print them again but now to 7 decimal places each
        // teensy_device.sendInfo("Gain Calibration 1: %.7f", gainCalibration1); // Should be 16/22
        // teensy_device.sendInfo("Gain Calibration 2: %.7f", gainCalibration2); // Should be 8/22
        // teensy_device.sendInfo("Gain Calibration 3: %.7f", gainCalibration3); // Should be 8/22

    } 

    // This is separate so it can get in the same loop iteration as the above - i.e. once we reach the end of process we are finished! 
    if (calibrationStep == maxCalibrationStep){
        // First we check if the gain calibration factors are somehow reasonable, note this could go wrong if we do not have the circuit set up properly e.g if the RTD is disconnected:
        if (gainCalibration1> 0.0001 && gainCalibration2 > 0.0001 && gainCalibration3 > 0.0001){ // Note if RTD is disconnected some/all of these could be zero! 
            // Calculate anything needed with calibration factor to finish the calibration process.
            gainCalibrationFactor = 3.0/(gainCalibration1 * (22.0/16.0) + gainCalibration2* (22.0/8.0) + gainCalibration3* (22.0/8.0)) ; // This is the average of the three gain calibration factors. In absence of better hypothesis we just do weighted sum.
        } else {
            // If we have a problem with the calibration factors we set the gain calibration factor to 1.0 which is the default value.
            gainCalibrationFactor = 1.0;
            teensy_device.sendError("RTD Calibration went wrong somehow, maybe RTD1 disconnected?");
        }

        // our tests put this number JUST below 1 by approximately 50ppm. 
        calibrationStep = -1; // Set it to -1 which means we are finished

    }

    return calibrationStep;

}

float ADS124S08::getCalibrationFactor(){
    return gainCalibrationFactor;
}