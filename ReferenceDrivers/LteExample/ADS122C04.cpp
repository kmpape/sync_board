#include "ADS122C04.h"
#include "Teensy_HID.h"

// Default constructor
ADS122C04::ADS122C04() : i2caddr(0) {
  teensy_device.sendError("Bad key provided for ADS122C04");  
  // Default initialization if needed
}

// The chip Address
ADS122C04::ADS122C04(uint8_t addr) : i2caddr(addr){
}

//initilise the chip with default settings for the board
void ADS122C04::Initialise()
{
    // Set the default configuration for the ADS122C04
    //  sendCommand(START);
    
     uint8_t CONFIG0 = 0x00 |
                        (MUX_AIN0_NEG << 4); // Set the input to be the correct way round considering our negative and positive terminals
                        (GAIN_2 << 1);    // Set the gain

     uint8_t CONFIG1 = 0x00 |
                        (CONTINUOUS << 3) | // Set the conversion mode to SINGLE_SHOT OR CONTINUOUS
                        (VREF_INTERNAL << 1);   // Set what to use as the reference voltage, either interal VREF_INTERNAL or external VREF_AVDD
    
     writeReg(CONFIG_0_REG, CONFIG0); // Write the configuration to the CONFIG_1 register
     writeReg(CONFIG_1_REG, CONFIG1); // Write the configuration to the CONFIG_1 register

    Wire1.beginTransmission(i2caddr);
    Wire1.write(START); // Send the START command to begin conversions
    uint8_t result = Wire1.endTransmission();
    if (result != 0) {
        teensy_device.sendError("ADS122C04 0x%X: Failed to start conversions. Error: %d)", i2caddr, result);
    }
}

//Reading register values 
uint8_t ADS122C04::readReg(uint8_t reg)
{
  uint8_t result;
  // Begin transmission to the device
  Wire1.beginTransmission(i2caddr);
  
  // Send the single-byte read command
  Wire1.write(RREG | (reg << 2)); // RREG command with register address

  Wire1.endTransmission(false); // Use repeated start to keep the bus active

  Wire1.requestFrom(i2caddr, (uint8_t)1); // Now request 1 byte from the device

  // Read that byte and store it
  result = Wire1.read();

  return result;
}

//writing to reg to initialise the chip 
void ADS122C04::writeReg(uint8_t reg, uint8_t writeValue)
{
   // Begin transmission to the device
   Wire1.beginTransmission(i2caddr);
  
   // Send the single-byte read command
   Wire1.write(WREG | (reg << 2)); // RREG command with register address
  
   Wire1.write(writeValue); // Send the value to write to the register

   uint8_t transmission_status = Wire1.endTransmission(); // End the transmission

    if (transmission_status != 0) {
        teensy_device.sendError("ADS122C04 0x%X: Failed write reg: %X value: %X", i2caddr, reg, writeValue);
    } 
  
}

uint8_t ADS122C04::sendCommand(uint8_t command)
{
  Wire1.beginTransmission(i2caddr);
  Wire1.write(command);
  uint8_t write_state = Wire1.endTransmission();
  return write_state;
}

bool ADS122C04::checkDataReady(void)
{
  uint8_t reg2val = readReg(CONFIG_2_REG);
  // In the ADS122C04 datasheet, bit 7 of Config Register 2 is DRDY.
  //   1 => data is ready
  //   0 => data is not ready
  //
  bool dataReady = ((reg2val >> 7) == 0b1);
  return dataReady;
}

// void ADS122C04::startConversion(void)
// {
//   Wire1.beginTransmission(i2caddr);
//   Wire1.write(START);
//   Wire1.endTransmission();
// }

float ADS122C04::readRawVoltage(uint8_t rate)
{

  // ADC set to continuous conversion mode, so we just need to read the data register
  
  uint8_t dataByte[3] = {0};
  Wire1.beginTransmission(i2caddr);
  Wire1.write(RDATA);
  Wire1.endTransmission(false); // Use repeated start to keep the bus active

  Wire1.requestFrom(i2caddr, (uint8_t)3);

  // Read the three bytes (MSB to LSB)
  dataByte[0] = Wire1.read(); // MSB
  dataByte[1] = Wire1.read();
  dataByte[2] = Wire1.read(); // LSB

  // 3) Reassemble the 24-bit data into a 32-bit container:
  uint32_t rawData = (((uint32_t)dataByte[0] << 16) | ((uint32_t)dataByte[1] << 8) | ((uint32_t)dataByte[2]));

  // 4) Convert the raw 24-bit value into a signed 32-bit integer:
  if ((rawData >> 23) & 0b1)  // If the sign bit (24th bit) is set...
  {
    rawData |= 0xFF000000; // sign-extend the negative value into 32 bits
  }
  int32_t signedData = static_cast<int32_t>(rawData); // Convert to signed 32-bit integer

  // 5) Convert the signed data into an analog voltage value:
  //    The LSB voltage is: (VREF / Gain) / (2^23). For example, with VREF = 2.048V and GAIN = 1.
  float gainUsed = 2.0f;        // Will change dependent on register config0
  float vRef     = 2.048f;       // 2.048V internal reference
  float lsb      = (vRef / gainUsed) / 8388608.0f; // 2^23 = 8,388,608
  float voltage = static_cast<float>(signedData) * lsb;

  // 6) Return the computed voltage
  return voltage;
}

