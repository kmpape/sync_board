#include "FXLS8964.h"
#include "Teensy_HID.h"

FXLS8964::FXLS8964() : i2caddr(0) {
    teensy_device.sendError("Bad key provided for FXLS8964");
    // Default initialization if needed
}

FXLS8964::FXLS8964(uint8_t addr) : i2caddr(addr) {
    // Initialize the FXLS8964 sensor
}

void FXLS8964::Initialise() {
    uint8_t SENS_CONFIG1 = 
        (0b0  << 7) | // RST
        (0b00 << 5) | // Self test function
        (0b0  << 4) | // Self test polarity
        (0b0  << 3) | // SPI interface mode
        ((FSR & 0b11) << 1) | // Full scale measurement range: 0b00 = ±2g, 0b01 = ±4g, 0b10 = ±8g, 0b11 = ±16g
        (0b1  << 0) | // Standby/Active mode selection: 0b0 = standby, 0b1 = active
        (0x00);       
    
    uint8_t SENS_CONFIG2 = 
        (0b00 << 6) | // WAKE power mode: 0b00 low power mode, 0b01 high performance mode, 0b10 or 0b11 Flexible mode
        (0b00 << 4) | // Sleep power mode: 0b00 low power mode, 0b01 high performance mode, 0b10 or 0b11 Flexible mode
        (0b0  << 3) | // Little/Big-endian output mode selection: 0b0 LE, 0b1 BE
        (0b0  << 1) | // Temperature output data auto-increment control: 0b0 Temp not included in auto increment [AI] range, 0b1 temp is included when AI
        (0b0  << 0) | // Fast read mode selection: 0b0 normal, 0b1 fast
        (0x00);   

    uint8_t SENS_CONFIG3 = 
        (0b0000 << 4) | // Wake ODR/Wake DEC
        (0b0000 << 0) | // Sleep ODR/Sleep DEC
        (0x00);       
    
    uint8_t SENS_CONFIG4 = 
        (0b0  << 7) | // External trigger acquisition mode: 
        (0b0  << 6) | // Wake SDCD WT - when value within threshold trigger a wake 
        (0b1  << 5) | // Wake SDCD OT - when value outside threshold trigger a wake
        (0b0  << 4) | // Wake Orient
        (0b0  << 3) | // DRDY_PUL
        (0b0  << 2) | // INT2_FUNC
        (0b0  << 1) | // INT_PP_OD: 0 interrupt output pin drier is push-pull, 1 interrupt pin is open-drain/open-source
        (0b1  << 0) | // INT_POL: 0 active low interrupt, 1 active high interrupt
        (0x00);   
    
    uint8_t SENS_CONFIG5 = 
        (0b1  << 4) | // Vector magnitude calculation enable: 0 VECM not calculated on ODR cycle and not included in AI, 1 calculated and included in AI
        (0b0  << 3) | // X_DIS: 0 X axis enabled, 1 X axis disabled
        (0b0  << 2) | // Y_DIS: 0 Y axis enabled, 1 Y axis disabled
        (0b0  << 1) | // Z_DIS: 0 Z axis enabled, 1 Z axis disabled
        (0b0  << 0) | // Hibernate mode enable: 0 Hibernate mode disabled, 1 enabled 
        (0x00);

    uint8_t INT_EN =
        (0b0  << 7) | // DRDY_EN sends interrupt when data is ready to be read: 0 disabled, 1 enabled
        (0b0  << 6) | // BUF_EN output data buffer interrupt output enable: 0 disabled, 1 enabled
        (0b1  << 5) | // SDCD_OT_EN SDCD outside threshold interrupt enable: 0 disabled, 1 enabled
        (0b0  << 4) | // SDCD_WT_EN SDCD within threshold interrupt enable: 0 disabled, 1 enabled
        (0b0  << 3) | // Orientation change interrupt enable: 0 disabled, 1 enabled
        (0b0  << 2) | // Auto-wake/sleep interrupt output enable: 0 disabled, 1 enabled
        (0b1  << 1) | // Boot interrupt output disable: 1 disabled, 0 enabled
        (0b0  << 0) | // Signal whether device is awake on int pin: 0 disabled, 1 enabled
        (0x00); // reserved

    uint8_t INT_PIN_SEL =
        (0b0  << 7) | // DRDY_INT2   :   0 int1 pin used, 1 int2 pin used
        (0b0  << 6) | // BUF_INT2    :   0 int1 pin used, 1 int2 pin used
        (0b1  << 5) | // SDCD_OT_INT2:   0 int1 pin used, 1 int2 pin used - from altium should this be 0?
        (0b0  << 4) | // SDCD_WT_INT2:   0 int1 pin used, 1 int2 pin used    
        (0b0  << 3) | // Orient_INT2 :   0 int1 pin used, 1 int2 pin used
        (0b0  << 2) | // ASLP_INT2   :   0 int1 pin used, 1 int2 pin used
        (0b0  << 1) | // BOOT_INT2   :   0 int1 pin used, 1 int2 pin used
        (0b0  << 0) | // WAKE_INT2   :   0 int1 pin used, 1 int2 pin used
        (0x00); // reserved

    /*
    With outside-of-thresholds event latching enabled, the X/Y/Z_OT_EF flags can only be cleared
    by reading the SDCD_INT_SRC1 register. When OT ELE is set, the outside-of-thresholds event
    condition (OT_EA) flag is only asserted when the event condition transitions from False to True. For
    example, once the OT_EA flag is set, the event condition must become False before a subsequent
    True event is recognized. With outside-of-thresholds event latching disabled, the X/Y/Z OT_EF flags
    are updated in real time and may be cleared by the function prior to the host system reading SDCD_
    INT_SRC1 if the event condition becomes false before the read occurs.
    */
    uint8_t SDCD_CONFIG1 = 
        (0b0  << 7) | // OT_ELE outside threshold latching enabled - : 0 disabled, 1 enabled
        (0b0  << 6) | // WT_ELE: 0 disabled, 1 enabled
        (0b1  << 5) | // X_OT_EN: 0 disabled, 1 enabled - 0 X-axis data or delta is not used in the outside of thresholds condition evaluation.
        (0b1  << 4) | // Y_OT_EN: 0 disabled, 1 enabled
        (0b1  << 3) | // Z_OT_EN: 0 disabled, 1 enabled
        (0b0  << 2) | // X_WT_EN: 0 disabled, 1 enabled
        (0b0  << 1) | // Y_WT_EN: 0 disabled, 1 enabled
        (0b0  << 0) | // Z_WT_EN: 0 disabled, 1 enabled
        (0x00);

    int16_t SDCD_LTHS = convertToLSB(GLowerThresholdTrigger); // Lower threshold
    int16_t SDCD_UTHS = convertToLSB(GUpperThresholdTrigger); // Upper threshold

    Wire1.beginTransmission(i2caddr);
    Wire1.write(REG_FXLS8964_INT_STATUS);
    Wire1.endTransmission(false);
    Wire1.requestFrom(i2caddr, (uint8_t)1);
    uint8_t int_status = Wire1.read();
    if (!(int_status && 0b1)) {
        teensy_device.sendError("FXLS8964: Device still booting");
    } else {
        teensy_device.sendDebug("FXLS8964: Device boot sequence completed");
    }

    Wire1.beginTransmission(i2caddr);
    Wire1.write(REG_FXLS8964_SENS_CONFIG1); // Auto increments by default
    Wire1.write(SENS_CONFIG1);
    Wire1.write(SENS_CONFIG2);
    Wire1.write(SENS_CONFIG3);
    Wire1.write(SENS_CONFIG4);
    Wire1.write(SENS_CONFIG5);
    uint8_t result = Wire1.endTransmission();
    if (result != 0) {
        teensy_device.sendError("FXLS8964: Failed to set sensor configuration, Error code: %d", result);
    }

    Wire1.beginTransmission(i2caddr);
    Wire1.write(REG_FXLS8964_INT_EN); // Auto increments by default
    Wire1.write(INT_EN);
    Wire1.write(INT_PIN_SEL);
    uint8_t result2 = Wire1.endTransmission();
    if (result2 != 0) {
        teensy_device.sendError("FXLS8964: Failed to set interrupt configuration, Error code: %d", result2);
    }

    Wire1.beginTransmission(i2caddr);
    Wire1.write(REG_FXLS8964_SDCD_CONFIG1); // Auto increments by default
    Wire1.write(SDCD_CONFIG1);
    uint8_t result3 = Wire1.endTransmission();
    if (result3 != 0) {
        teensy_device.sendError("FXLS8964: Failed to set SDCD configuration, Error code: %d", result3);
    }

    Wire1.beginTransmission(i2caddr);
    Wire1.write(REG_FXLS8964_SDCD_LTHS_LSB); // Auto increments by default
    Wire1.write(SDCD_LTHS & 0xFF); // LSB
    Wire1.write((SDCD_LTHS >> 8) & 0xFF); // MSB
    Wire1.write(SDCD_UTHS & 0xFF); // LSB
    Wire1.write((SDCD_UTHS >> 8) & 0xFF); // MSB
    uint8_t result4 = Wire1.endTransmission();
    if (result4 != 0) {
        teensy_device.sendError("FXLS8964: Failed to set SDCD thresholds, Error code: %d", result4);
    }

}


float FXLS8964::convertToG(int16_t rawData) {
    // Convert the raw data to g's
    // rawData is in 12bit two's complement format with extended sign
    uint8_t accel_range_plusminus = 1 << (FSR + 1); // ±2g, ±4g, ±8g, ±16g dependent on FSR | 2^(FSR + 1)
    float gValue = float(rawData)/2048.0 * float(accel_range_plusminus);
    return gValue;
}

int16_t FXLS8964::convertToLSB(float gValue) {
    // Convert the g value to raw data
    // rawData is in 12bit two's complement format with extended sign
    uint8_t accel_range_plusminus = 1 << (FSR + 1); // ±2g, ±4g, ±8g, ±16g dependent on FSR | 2^(FSR + 1)
    int16_t rawData = int16_t((gValue/float(accel_range_plusminus))*2048.0);
    return rawData;
}

int8_t FXLS8964::readTemperature() {
    Wire1.beginTransmission(i2caddr);
    Wire1.write(REG_FXLS8964_TEMP_OUT); // Register address for temperature
    Wire1.endTransmission(false);

    Wire1.requestFrom(i2caddr, (uint8_t)1);
    int8_t temperature = Wire1.read();
    temperature = temperature + 25; // offset by 25 deg C, 0x00 = 25degC
    return temperature;
}

FXLS8964::FXLS8964Data FXLS8964::readAccel() {
    FXLS8964Data data;
    Wire1.beginTransmission(i2caddr);
    Wire1.write(REG_FXLS8964_OUT_X_LSB); // Register address for X axis LSB
    Wire1.endTransmission(false);
    // teensy_device.sendInfo("Reading Accelerometer Data");

    Wire1.requestFrom(i2caddr, (uint8_t)6);

    uint16_t rawX = Wire1.read() | (Wire1.read() << 8); // Read X axis data
    uint16_t rawY = Wire1.read() | (Wire1.read() << 8); // Read Y axis data
    uint16_t rawZ = Wire1.read() | (Wire1.read() << 8); // Read Z axis data

    data.x = convertToG(rawX); // ; // Convert to g's
    data.y = convertToG(rawY); // ; // Convert to g's
    data.z = convertToG(rawZ); // ; // Convert to g's

    return data;
}


uint8_t FXLS8964::clearInterrupt() {
    // teensy_device.sendInfo("Interrupt protocol called");
    FXLS8964Data data = readAccel();

    FXLS8964Data base_val;
    base_val.x = 0;
    base_val.y = 0;
    base_val.z = 1;

    teensy_device.sendInfo("%.2f", data.x);

    if (data.x - base_val.x > GUpperThresholdTrigger){
        teensy_device.sendInfo("X axis triggered above threshold");
    } else if (data.x - base_val.x < GLowerThresholdTrigger){
        teensy_device.sendInfo("X axis triggered below threshold");
    }

    if (data.y - base_val.y > GUpperThresholdTrigger){
        teensy_device.sendInfo("Y axis triggered above threshold");
    } else if (data.y - base_val.y < GLowerThresholdTrigger){
        teensy_device.sendInfo("Y axis triggered below threshold");
    }

    if (data.z - base_val.z > GUpperThresholdTrigger){
        teensy_device.sendInfo("Z axis triggered above threshold");
    } else if (data.z - base_val.z < GLowerThresholdTrigger){
        teensy_device.sendInfo("Z axis triggered below threshold");
    }

    return 0;


}