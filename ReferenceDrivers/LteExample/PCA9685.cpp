#include "PCA9685.h"
#include "Teensy_HID.h"

// Default constructor
PCA9685::PCA9685() : i2caddr(0) {
    teensy_device.sendError("Bad key provided for PCA9685");
    // Default initialization if needed
}

PCA9685::PCA9685(uint8_t addr) {
    i2caddr = addr;
}

void PCA9685::Initialise()  // THIS should turn everythhing to the default safe/startup state
{
    AutoIncr(true);  // by default enable auto increment

    // The default frequency will be 200 hz
    // We will probably just set the frequency once and leave it, but will vary the PWM duty cycle
    switch (i2caddr) {
        case Heatboard_PWM_1_ADR:  // Reactor Heaters and Fans
            break;
        case Heatboard_PWM_2_ADR:  // UV LEDs
            break;
        case Heatboard_PWM_3_ADR:  // Visible LEDs
            break;
        case Topboard_PWM_1_ADR:  // Strain Gauges
            EnableExternalClock();
            break;
        case Topboard_PWM_2_ADR:  // IMB Heaters
            SetPWM(6, 0);  // Explicitly set LEDPWM (the OE pin on IO chips) to have PWM duty cycle of 0/4095, so always on (active low)
            SetPWM(12, 0);  // Explicitly set THER to off
            SetPWM(13, 0);  // Explicitly set DIA to off
            break;
    }

    Wake();  // Wake up the chip 
    SetAllPWM(0); // Set all PWM channels to duty cycle 0, i.e. initialise them in the awake but off state.
}

// This returns the channel number i.e. LED0, LED1 from the input pin number, at reactor level we only store what pin is used, not the channel
uint8_t PCA9685::getChannel(uint8_t pin_number) {
    if (pin_number >= 6 && pin_number <= 13) {
        return pin_number - 6;
    } else if (pin_number >= 15 && pin_number <= 22) {
        return pin_number - 7;
    }
    return 0xFF;  // Return an invalid value if pin_number is out of range
}

// The AutoIncr function is used to enable or disable the auto increment feature of the PCA9685,
// this feature allows for multiple bytes to be written or read in a single I2C transaction by
// incrementing the register address after each byte is written or read.
void PCA9685::AutoIncr(bool state) {
    // First reads the current MODE1 register
    Wire1.beginTransmission(i2caddr);
    Wire1.write(0x00);  // MODE1 register
    Wire1.endTransmission();
    Wire1.requestFrom(i2caddr, (uint8_t)1);  // request one byte of data
    uint8_t mode1 = Wire1.read();

    if (state) {
        // Set the AI bit in MODE1 to be logic 1
        mode1 |= (1 << 5);
    } else {
        // Set the AI bit in MODE1 to be logic 0
        mode1 &= ~(1 << 5);
    }

    // Update the MODE1 register with modified byte
    Wire1.beginTransmission(i2caddr);
    Wire1.write(0x00);  // MODE1 register
    Wire1.write(mode1);
    int result = Wire1.endTransmission();
    if (result != 0) {
        teensy_device.sendError("PCA9685 0x%X: Failed to set Auto Increment, Error code: %d", i2caddr, result);
    }
}

// The default is for the internal clock to be used
// The external clock can be enabled by a chip reset
void PCA9685::EnableExternalClock() {
    ExternalClock = true;
    Wire1.beginTransmission(i2caddr);
    Wire1.write(0x00);  // MODE1 register
    Wire1.endTransmission();

    Wire1.requestFrom(i2caddr, (uint8_t)1);  // request one byte of data
    uint8_t mode1 = Wire1.read();

    // Set the SLEEP bit in MODE1
    mode1 |= 0x10;
    Wire1.beginTransmission(i2caddr);
    Wire1.write(0x00);  // MODE1 register
    Wire1.write(mode1);
    int result = Wire1.endTransmission();
    if (result != 0) {
        teensy_device.sendError("PCA9685 0x%X: Failed to set sleep bit, Error code: %d", i2caddr, result);
    }

    // Write logic 1s to both the SLEEP and EXTCLK bits in MODE1
    mode1 |= 0x50;
    Wire1.beginTransmission(i2caddr);
    Wire1.write(0x00);  // MODE1 register
    Wire1.write(mode1);
    int result2 = Wire1.endTransmission();
    if (result2 != 0) {
        teensy_device.sendError("PCA9685 0x%X: Failed to set external clock, Error code: %d", i2caddr, result2);
    }
}

void PCA9685::Sleep() {
    // Set the SLEEP bit in MODE1 to logic 1
    Wire1.beginTransmission(i2caddr);
    Wire1.write(0x00);  // MODE1 register
    Wire1.endTransmission();

    Wire1.requestFrom(i2caddr, (uint8_t)1);  // request one byte of data
    uint8_t mode1 = Wire1.read();

    // Set the SLEEP bit in MODE1
    mode1 |= 0x10;
    Wire1.beginTransmission(i2caddr);
    Wire1.write(0x00);  // MODE1 register
    Wire1.write(mode1);
    int result = Wire1.endTransmission();
    if (result != 0) {
        teensy_device.sendError("PCA9685 0x%X: Failed to set sleep bit, Error code: %d", i2caddr, result);
    }
}

void PCA9685::Wake() {
    // Set the SLEEP bit in MODE1 to logic 0

    // First reads the current MODE1 register
    Wire1.beginTransmission(i2caddr);
    Wire1.write(0x00);  // MODE1 register
    Wire1.endTransmission();
    Wire1.requestFrom(i2caddr, (uint8_t)1);  // request one byte of data
    uint8_t mode1 = Wire1.read();

    // Set the SLEEP bit in MODE1 to be logic 0
    mode1 &= ~(1 << 4);

    // Update the MODE1 register with modified byte
    Wire1.beginTransmission(i2caddr);
    Wire1.write(0x00);  // MODE1 register
    Wire1.write(mode1);
    int result = Wire1.endTransmission();
    if (result != 0) {
        teensy_device.sendError("PCA9685 0x%X: Failed to wake up, Error code: %d", i2caddr, result);
    }

    // Wait for oscillator to stabilize after waking up (requires 500ms according to datasheet)
    delay(wakeup_delay_ms);
}

void PCA9685::SetFrequency(uint16_t freq, uint32_t clk_freq ) {
    // Calculates the prescale value to use for the requested frequency and external clock frequency
    // The formula is prescale = round(ClockFrequency / (4096 * Frequency)) - 1
    // Minimum frequency is 24 Hz, maximum frequency is 1526 Hz (when using the internal clock)
    uint8_t prescale_val = round(clk_freq / (4096 * freq)) - 1;
    if (prescale_val < 3) {
        prescale_val = 3;
    } else if (prescale_val > 255) {
        prescale_val = 255;
    }

    // The PRE_SCALE register can only be set when the SLEEP bit of MODE1 register is set to logic 1.
    Sleep();

    Wire1.beginTransmission(i2caddr);
    Wire1.write(0xFE);  // PRE_SCALE register
    Wire1.write(prescale_val);
    int result = Wire1.endTransmission();
    if (result != 0) {
        teensy_device.sendError("PCA9685 0x%X: Failed to set frequency, Error code: %d", i2caddr, result);
    }
    // Wake up the chip after setting to new frequency
    Wake();
}

// counts between 0 and 4095
// on time determines when the led will be turned on
// off time determines when the led will be turned off
// if off = on then the led will be always off
void PCA9685::SetPWM(uint8_t pin, uint16_t on, uint16_t off) {
    uint8_t channel = getChannel(pin);
    uint8_t on_lsb = on & 0xFF;          // not strictly needed but to make it clear that its the lower 8 bits
    uint8_t on_msb = (on & 0xF00) >> 8;  // takes the upper 4 bits of the 12 bit value

    uint8_t off_lsb = off & 0xFF;          // not strictly needed but to make it clear that its the lower 8 bits
    uint8_t off_msb = (off & 0xF00) >> 8;  // takes the upper 4 bits of the 12 bit value

    Wire1.beginTransmission(i2caddr);
    Wire1.write(0x06 + 4 * channel);  // LED0 register is 0x06, each LED register is 4 bytes

    Wire1.write(on_lsb);
    Wire1.write(on_msb);
    Wire1.write(off_lsb);
    Wire1.write(off_msb);

    int result = Wire1.endTransmission();
    if (result != 0) {
        teensy_device.sendError("PCA9685 0x%X: Failed to set PWM for pin %d, Error code: %d", i2caddr, pin, result);
    }
}

// duty cycle is a value between 0 and 4096
void PCA9685::SetPWM(uint8_t pin, uint16_t duty_cycle_12bit) {
    uint8_t channel = getChannel(pin);

    /*
    If Duty cycle = 0 want to be off for the whole time
    If Duty cycle = 4096 want to be on for the whole time
    */
   uint8_t on_lsb  ;
   uint8_t on_msb  ;
   uint8_t off_lsb ;
   uint8_t off_msb ;
    
    if (duty_cycle_12bit == 4096){
        // teensy_device.sendDebug("Mode: 4096");
        // If 4095 requested then full on is wanted 
        on_lsb = 0xFF; // sets the delay for when it should turn on the first cycle after receiving the command, 0 should be mean as quick as possible
        on_msb = 0x1F; // The 1 in bit 4 signals that it should be fully on
        off_lsb = 0x00; // this is irrelevant as it has been set to be fully on
        off_msb = 0x00; // this is irrelevant as it has been set to be fully on

    } else if (duty_cycle_12bit == 0){
        // teensy_device.sendDebug("Mode: 0");
        // If 0 requested then full off is wanted 
        on_lsb = 0x00;  
        on_msb = 0x00;  
        off_lsb = 0x00; 
        off_msb = 0x10; // same as full on, bit 4 of msb signals fully off

    } else {
        // teensy_device.sendDebug("Mode: Default");
        // If neither of the above then we need to calculate the on and off times
        on_lsb = 0x00;  // always start at 0, i.e. no offset of start time
        on_msb = 0x00;
        off_lsb = duty_cycle_12bit & 0xFF;
        off_msb = (duty_cycle_12bit & 0xFF00) >> 8;

    }

    Wire1.beginTransmission(i2caddr);
    Wire1.write(0x06 + 4 * channel);  // LED0 register is 0x06, each LED register is 4 bytes

    Wire1.write(on_lsb);
    Wire1.write(on_msb);
    Wire1.write(off_lsb);
    Wire1.write(off_msb);

    int transmission_success = Wire1.endTransmission();
    if (transmission_success != 0) {
        teensy_device.sendError("Failed to set PWM for pin %d on PCA9685 at address 0x%02X. Error code: %d", pin, i2caddr, transmission_success);
    }

}

void PCA9685::SetAllPWM(uint16_t duty_cycle_12bit) {
    /*
    If Duty cycle = 0 want to be off for the whole time
    If Duty cycle = 4096 want to be on for the whole time
    */
   uint8_t on_lsb  ;
   uint8_t on_msb  ;
   uint8_t off_lsb ;
   uint8_t off_msb ;
    
    if (duty_cycle_12bit == 4096){
        // teensy_device.sendDebug("Mode: 4096");
        // If 4095 requested then full on is wanted 
        on_lsb = 0xFF; // sets the delay for when it should turn on the first cycle after receiving the command, 0 should be mean as quick as possible
        on_msb = 0x1F; // The 1 in bit 4 signals that it should be fully on
        off_lsb = 0x00; // this is irrelevant as it has been set to be fully on
        off_msb = 0x00; // this is irrelevant as it has been set to be fully on

    } else if (duty_cycle_12bit == 0){
        // teensy_device.sendDebug("Mode: 0");
        // If 0 requested then full off is wanted 
        on_lsb = 0x00;  
        on_msb = 0x00;  
        off_lsb = 0x00; 
        off_msb = 0x10; // same as full on, bit 4 of msb signals fully off

    } else {
        // teensy_device.sendDebug("Mode: Default");
        // If neither of the above then we need to calculate the on and off times
        on_lsb = 0x00;  // always start at 0, i.e. no offset of start time
        on_msb = 0x00;
        off_lsb = duty_cycle_12bit & 0xFF;
        off_msb = (duty_cycle_12bit & 0xFF00) >> 8;

    }

    Wire1.beginTransmission(i2caddr);
    Wire1.write(0xFA);  // Register for controlling all channels
    
    Wire1.write(on_lsb);
    Wire1.write(on_msb);
    Wire1.write(off_lsb);
    Wire1.write(off_msb);

    int transmission_success = Wire1.endTransmission();
    if (transmission_success != 0) {
        teensy_device.sendError("Failed to set PWM for all pins on PCA9685 at address 0x%02X. Error code: %d", i2caddr, transmission_success);
    }
}