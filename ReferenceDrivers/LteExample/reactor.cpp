#include "reactor.h"
#include "Teensy_HID.h"

extern "C" {
    #include <packet_config.pb.h>
}

// // Dummy static maps so the references in the default constructor can bind to something
// static std::map<CHIPS, PCA9505> dummy_io_chips;
// static std::map<CHIPS, PCA9685> dummy_pwm_chips;
// static std::map<CHIPS, AD5673> dummy_dac_chips;

// Default constructor definition
Reactor::Reactor() : reactor_name(REACTOR::NONE) {
    teensy_device.sendError("Invalid system name provided for Reactor constructor.");
    // No pinMap setup here
    setPinMap();
}

Reactor::Reactor(REACTOR::Type name) : reactor_name(name)
{
    // dont do anything here that calls an i2c read or write, as the i2c bus is not enabled until system enable is true which only gets called after this
    setPinMap();
}

void Reactor::setToDefault(){

    teensy_device.sendDebug("Setting up reactor " + (int)reactor_name);

    HeaterPIDState = false; //default is false
    pid_packet_id = 0;
    send_pid_packets = false;
    
    p_term = 0.0;
    i_term = 0.0;
    d_term = 0.0;
    error = 0.0;

    setIRWavelength(850);  // Set the IR LED to be 850nm as an arbitrary initial value
    setIRIO(false);  // Turn off the IR LED to start
    selectAdsorptionPD(true);  // Set the PD to be the absorption PD as default turnon state 1=absorption
    setPDFilterFastMode(true);  // Set the PD filter to be the fast filter as default turnon state, this is channel A, sinc5 filter, ADC filter type 1
    setSYNC();  // Turn on the sync signal to the IR LED
    enableSYNC(true);  // Enable the sync signal to the IR LED
   
    // Must read the StirFault pin to clear any fault flags and enable operation
    setStirIO(false); // Turn off StirEnable so it should not be doing anythign .

    setMicrostepsPerFullStep(256);  // see setMicrostep for available options
    // m1 = 2.1v

    setStirPower(25.0);  // initialise the stirring with a default power setting of 25.0 to reduce heat

    setStirSpeed(1500.0);  // Set the stirring speed to 1500 rpm as default
}

void Reactor::setPinMap() {
    if (reactor_name == REACTOR::AL) {
        pinMap.M0             = 13;
        pinMap.M1             = 11;
        pinMap.StirEnable     = 12;
        pinMap.StirCurrent    = 12;
        pinMap.StirFault      = 14;
        pinMap.StirStep       = STIR_STEP_PIN_AL;
        pinMap.UV             = 6;
        pinMap.INN            = 12;
        pinMap.DA             = 3;
        pinMap.DB             = 4;
        pinMap.DC             = 5;
        pinMap.DF             = 41;
        pinMap.IR_SYNC        = IR_SYNC_PIN_AL;
        pinMap.ADC_Channel    = 7;
        pinMap.SLA            = 3;
        pinMap.SLB            = 4;
        pinMap.LDA            = 6;
        pinMap.LDB            = 7;
        pinMap.sync_frequency = 800;
        heatboard_io_chip     = &CHIPS.Heatboard_IO_1;

    } else if (reactor_name == REACTOR::AR) {
        pinMap.M0            = 25;
        pinMap.M1            = 9;
        pinMap.StirEnable    = 24;
        pinMap.StirCurrent   = 10;
        pinMap.StirFault     = 26;
        pinMap.StirStep      = STIR_STEP_PIN_AR;
        pinMap.UV            = 7;
        pinMap.INN           = 10;
        pinMap.DA            = 7;
        pinMap.DB            = 8;
        pinMap.DC            = 9;
        pinMap.DF            = 42;
        pinMap.IR_SYNC       = IR_SYNC_PIN_AR;
        pinMap.ADC_Channel   = 6;
        pinMap.SLA           = 15;
        pinMap.SLB           = 16;
        pinMap.LDA           = 8;
        pinMap.LDB           = 9;
        pinMap.sync_frequency = 1200;
        heatboard_io_chip     = &CHIPS.Heatboard_IO_1;

    } else if (reactor_name == REACTOR::BL) {
        pinMap.M0            = 41;
        pinMap.M1            = 7;
        pinMap.StirEnable    = 40;
        pinMap.StirCurrent   = 8;
        pinMap.StirFault     = 42;
        pinMap.StirStep      = STIR_STEP_PIN_BL;
        pinMap.UV            = 8;
        pinMap.INN           = 8;
        pinMap.DA            = 10;
        pinMap.DB            = 12;
        pinMap.DC            = 13;
        pinMap.DF            = 43;
        pinMap.IR_SYNC       = IR_SYNC_PIN_BL;
        pinMap.ADC_Channel   = 5;
        pinMap.SLA           = 31;
        pinMap.SLB           = 32;
        pinMap.LDA           = 10;
        pinMap.LDB           = 11;
        pinMap.sync_frequency = 1000;
        heatboard_io_chip     = &CHIPS.Heatboard_IO_1;

    } else if (reactor_name == REACTOR::BR) {
        pinMap.M0            = 53;
        pinMap.M1            = 14;
        pinMap.StirEnable    = 52;
        pinMap.StirCurrent   = 13;
        pinMap.StirFault     = 54;
        pinMap.StirStep      = STIR_STEP_PIN_BR;
        pinMap.UV            = 9;
        pinMap.INN           = 6;
        pinMap.DA            = 14;
        pinMap.DB            = 15;
        pinMap.DC            = 16;
        pinMap.DF            = 44;
        pinMap.IR_SYNC       = IR_SYNC_PIN_BR;
        pinMap.ADC_Channel   = 4;
        pinMap.SLA           = 43;
        pinMap.SLB           = 44;
        pinMap.LDA           = 12;
        pinMap.LDB           = 13;
        pinMap.sync_frequency = 800;
        heatboard_io_chip     = &CHIPS.Heatboard_IO_1;

    } else if (reactor_name == REACTOR::CL) {
        pinMap.M0            = 13;
        pinMap.M1            = 22;
        pinMap.StirEnable    = 12;
        pinMap.StirCurrent   = 23;
        pinMap.StirFault     = 14;
        pinMap.StirStep      = STIR_STEP_PIN_CL;
        pinMap.UV            = 10;
        pinMap.INN           = 13;
        pinMap.DA            = 17;
        pinMap.DB            = 19;
        pinMap.DC            = 20;
        pinMap.DF            = 36;
        pinMap.IR_SYNC       = IR_SYNC_PIN_CL;
        pinMap.ADC_Channel   = 0;
        pinMap.SLA           = 3;
        pinMap.SLB           = 4;
        pinMap.LDA           = 16;
        pinMap.LDB           = 15;
        pinMap.sync_frequency = 1200;
        heatboard_io_chip     = &CHIPS.Heatboard_IO_2;

    } else if (reactor_name == REACTOR::CR) {
        pinMap.M0            = 25;
        pinMap.M1            = 24;
        pinMap.StirEnable    = 24;
        pinMap.StirCurrent   = 25;
        pinMap.StirFault     = 26;
        pinMap.StirStep      = STIR_STEP_PIN_CR;
        pinMap.UV            = 11;
        pinMap.INN           = 11;
        pinMap.DA            = 21;
        pinMap.DB            = 22;
        pinMap.DC            = 24;
        pinMap.DF            = 37;
        pinMap.IR_SYNC       = IR_SYNC_PIN_CR;
        pinMap.ADC_Channel   = 1;
        pinMap.SLA           = 15;
        pinMap.SLB           = 16;
        pinMap.LDA           = 18;
        pinMap.LDB           = 17;
        pinMap.sync_frequency = 1000;
        heatboard_io_chip     = &CHIPS.Heatboard_IO_2;

    } else if (reactor_name == REACTOR::DL) {
        pinMap.M0            = 41;
        pinMap.M1            = 26;
        pinMap.StirEnable    = 40;
        pinMap.StirCurrent   = 27;
        pinMap.StirFault     = 42;
        pinMap.StirStep      = STIR_STEP_PIN_DL;
        pinMap.UV            = 12;
        pinMap.INN           = 9;
        pinMap.DA            = 25;
        pinMap.DB            = 26;
        pinMap.DC            = 31;
        pinMap.DF            = 38;
        pinMap.IR_SYNC       = IR_SYNC_PIN_DL;
        pinMap.ADC_Channel   = 2;
        pinMap.SLA           = 31;
        pinMap.SLB           = 32;
        pinMap.LDA           = 20;
        pinMap.LDB           = 19;
        pinMap.sync_frequency = 800;
        heatboard_io_chip     = &CHIPS.Heatboard_IO_2;

    } else if (reactor_name == REACTOR::DR) {
        pinMap.M0            = 53;
        pinMap.M1            = 28;
        pinMap.StirEnable    = 52;
        pinMap.StirCurrent   = 1;
        pinMap.StirFault     = 54;
        pinMap.StirStep      = STIR_STEP_PIN_DR;
        pinMap.UV            = 13;
        pinMap.INN           = 7;
        pinMap.DA            = 32;
        pinMap.DB            = 33;
        pinMap.DC            = 35;
        pinMap.DF            = 40;
        pinMap.IR_SYNC       = IR_SYNC_PIN_DR;
        pinMap.ADC_Channel   = 3;
        pinMap.SLA           = 43;
        pinMap.SLB           = 44;
        pinMap.LDA           = 22;
        pinMap.LDB           = 21;
        pinMap.sync_frequency = 1200;
        heatboard_io_chip     = &CHIPS.Heatboard_IO_2;

    } else {
        // Default/invalid config
        pinMap.M0            = 0;
        pinMap.M1            = 0;
        pinMap.StirEnable    = 0;
        pinMap.StirCurrent   = 0;
        pinMap.StirFault     = 0;
        pinMap.StirStep      = 0;
        pinMap.UV            = 0;
        pinMap.INN           = 0;
        pinMap.DA            = 0;
        pinMap.DB            = 0;
        pinMap.DC            = 0;
        pinMap.DF            = 0;
        pinMap.IR_SYNC       = 0;
        pinMap.ADC_Channel   = 0;
        pinMap.SLA           = 0;
        pinMap.SLB           = 0;
        pinMap.LDA           = 0;
        pinMap.LDB           = 0;
        pinMap.sync_frequency = 0;
    }
}

void Reactor::setSYNC() {
    // Set the SYNC signal for the IR LEDs
    // This is an approx 1kHz signal that is used in the lock in detection on the IR LEDs.
    // Different reactors need different frequencies so they dont overlap in frequency domain. Below numbers have not been optimized
    analogWriteFrequency(pinMap.IR_SYNC, pinMap.sync_frequency);  // Set the frequency of the SYNC signal to 1kHz
}

////// STIRRIING ///////
void Reactor::setStirPower(float power_percentage) {
    // if (power_percentage == stirPower) {
    //     return; // No change in power, so return early
    // }
    stirPower = power_percentage;
    // converts a percentage between 0 and 100% to a float value between 0-2.64V
    float output_voltage = (power_percentage / 100.0) * stir_max_power_ref_voltage;
    CHIPS.Heatboard_DAC.SetVoltage(pinMap.StirCurrent, output_voltage);  // Set the StirCurrent pin on the AD5673
}

float Reactor::getStirPower() {
    // Placeholder: return the current stirPower value
    return stirPower;
}

void Reactor::setMicrostepsPerFullStep(uint16_t steps) {
    /*
    1   - good
    2   - good when set to non circ half step
    4   - good
    8   - good
    16  - 2x speed (interpreted as 8)
    32  - good
    64  - full stepping (interpreted as full or full71%
    128 - 1/2 speed (interpreted as 256)
    256 - good
    */
    int m0_pin = pinMap.M0;
    int m1_pin = pinMap.M1;

    // if (steps == microstep_per_full_step) {
    //     return; // No change in microstepping, so return early
    // }
    
    if (steps == 1) {
        teensy_device.sendDebug("Setting microstepping to Full 100%");
        heatboard_io_chip->setPinMode(m0_pin, IO_OUTPUT);
        heatboard_io_chip->digitalPinWrite(m0_pin, IO_LOW);
        CHIPS.Heatboard_DAC.SetVoltage(m1_pin, 0.0);

    } else if (steps == 2) {
        teensy_device.sendDebug("Setting microstepping to non-circ 1/2 step");
        heatboard_io_chip->setPinMode(m0_pin, IO_OUTPUT);
        heatboard_io_chip->digitalPinWrite(m0_pin, IO_HIGH);
        CHIPS.Heatboard_DAC.SetVoltage(m1_pin, 0.0);

    } else if (steps == 4) {
        teensy_device.sendDebug("Setting microstepping to 1/4 step");
        heatboard_io_chip->setPinMode(m0_pin, IO_OUTPUT);
        heatboard_io_chip->digitalPinWrite(m0_pin, IO_LOW);
        CHIPS.Heatboard_DAC.SetVoltage(m1_pin, 5.0);

    } else if (steps == 8) {
        teensy_device.sendDebug("Setting microstepping to 1/8 step");
        heatboard_io_chip->setPinMode(m0_pin, IO_OUTPUT);
        heatboard_io_chip->digitalPinWrite(m0_pin, IO_HIGH);
        CHIPS.Heatboard_DAC.SetVoltage(m1_pin, 5.0);

    } else if (steps == 32) {
        teensy_device.sendDebug("Setting microstepping to 1/32 step");
        heatboard_io_chip->setPinMode(m0_pin, IO_OUTPUT);
        heatboard_io_chip->digitalPinWrite(m0_pin, IO_LOW);
        CHIPS.Heatboard_DAC.SetVoltage(m1_pin, 2.0);

    } else if (steps == 256) {
        teensy_device.sendDebug("Setting microstepping to 1/256 step");
        heatboard_io_chip->setPinMode(m0_pin, IO_OUTPUT);
        heatboard_io_chip->digitalPinWrite(m0_pin, IO_HIGH);
        CHIPS.Heatboard_DAC.SetVoltage(m1_pin, 2.0);

    } else {
        teensy_device.sendError("Not a valid microstepping level, choose from [1, 2, 4, 8, 32, 256]");
    }
    microstep_per_full_step = steps;
    setStirSpeed(stirSpeedRPM); // update the speed to reflect new stepping level
}

uint16_t Reactor::getMicrostepsPerFullStep() {
    // Placeholder implementation
    // Return an empty string or a default value until a real implementation is provided
    return microstep_per_full_step;
}

void Reactor::setStirSpeed(float speed_RPM) {
    // Set the speed of the stir bar
    // The speed of the stir bar is controlled by the PWM signal sent to the stir bar motor

    // convert speed_RPM into a frequency/256
    // this is dependent on microstepping level
    //  teensy_frequency_divider
    if (speed_RPM <= 0.0) {
        stirState = false; // Turn off the stir bar if speed is negative or 0
    } 
    // Stir chip has max frequency of 500kHz, so we need to limit the speed to be within this range
    // Clock divider has max frequency of 70 Mhz, since it divides by 256 this would be a stir chip frequency of 273 kHz, so this is the limiting factor
    float desired_frequency = (float(microstep_per_full_step) * speed_RPM * 360.0) / (degrees_rotation_per_step * seconds_per_minute);
    
    float teensy_frequency = desired_frequency * 256.0;
    float max_teensy_frequency = 50.0e6; // 1 MHz is the max frequency for the Teensy pin;
    if (teensy_frequency > max_teensy_frequency) {
        teensy_frequency = max_teensy_frequency; // Limit the frequency to a maximum of 1 MHz - the maximum frequency of the Teensy pin
        desired_frequency = teensy_frequency/256.0; // Limit the frequency to a maximum of 500 kHz - the maximum recommended frequency of the DRV8425
        speed_RPM = (desired_frequency * degrees_rotation_per_step * seconds_per_minute) / (float(microstep_per_full_step) * 360.0); // recalculate the speed_RPM to be the max speed
        teensy_device.sendError("Desired frequency is too high limit to %.2f RPM", speed_RPM);
    } 

    teensy_device.sendDebug("speed: %.1f RPM chip: %.2f Hz teensy: %.2f Hz", speed_RPM, desired_frequency, teensy_frequency);
    
    stirSpeedRPM = speed_RPM;

    analogWriteFrequency(pinMap.StirStep, teensy_frequency);
    analogWrite(pinMap.StirStep, 2048);  // Set the duty cycle to 50%
}

float Reactor::getStirSpeed() {
    // Placeholder: return current stir speed
    return stirSpeedRPM;
}

void Reactor::setStirIO(bool state) {
    // toggles the enable bit on the stirring chip to turn stirring off or on
    stirState = state;
    teensy_device.sendDebug("Set Reactor %d StirIO to %s", int(reactor_name), state ? "ON" : "OFF");
    heatboard_io_chip->digitalPinWrite(pinMap.StirEnable, state);  // Set the StirEnable pin on the PCA9505
}

bool Reactor::getStirIO() {
    // Placeholder: return current stir IO state
    return stirState;
}

////////////////// UV ///////////////////
void Reactor::setUVDutyCycle(uint16_t PWM_value_12bit) {
    // if the UV LED is turned on then set the PWM value
    teensy_device.sendDebug("Setting UV Duty Cycle to %d", PWM_value_12bit);
    if (PWM_value_12bit == 0) {
        UVState = false;
    } else if (PWM_value_12bit > 0) {
        UVState = true;
        UV_Duty_Cycle_12Bit = PWM_value_12bit; // store the duty cycle for when we turn it on again
    }
    CHIPS.Heatboard_PWM_2.SetPWM(pinMap.UV, PWM_value_12bit);
    
}

uint16_t Reactor::getUVDutyCycle() {
    // Placeholder implementation; return 0 until real logic is added
    return UV_Duty_Cycle_12Bit;
}


////////////////// LEDS ///////////////////
 void Reactor::setRGBColor(uint8_t IN_red, uint8_t IN_green, uint8_t IN_blue, uint8_t IN_intensity) {
    // teensy_device.sendError("Setting RGB Intensity to %d for colour R:%d G:%d B:%d", IN_intensity, IN_red, IN_green, IN_blue);
    if (IN_red == 0 && IN_green == 0 && IN_blue == 0) {
        // if all the provided RGB values are 0 use whatever is currently set
        rgb_intensity_8bit = IN_intensity;
    } else {
        // Update the values on the reactor class
        rgb_red_8bit        = IN_red;
        rgb_green_8bit      = IN_green;
        rgb_blue_8bit       = IN_blue;
        rgb_intensity_8bit  = IN_intensity;
    }
    
    // normalise rgb
    uint16_t total = rgb_red_8bit + rgb_green_8bit + rgb_blue_8bit;
    // float IN_red_norm       = float(rgb_red_8bit)     / float(total); not needed
    float IN_green_norm     = float(rgb_green_8bit)   / float(total);
    float IN_blue_norm      = float(rgb_blue_8bit)    / float(total);
    float IN_intensity_norm = float(rgb_intensity_8bit) / 255.0;

    // output
    uint16_t OUT_LDA_Start  = 0;
    uint16_t OUT_LDA_Stop   = ((IN_green_norm + IN_blue_norm) * (IN_intensity_norm * 4096.0));
    uint16_t OUT_LDB_Start  = (IN_green_norm * (IN_intensity_norm * 4096.0));
    uint16_t OUT_LDB_Stop   = (IN_intensity_norm * 4096.0);

    CHIPS.Heatboard_PWM_3.SetPWM(pinMap.LDA, OUT_LDA_Start, OUT_LDA_Stop);
    CHIPS.Heatboard_PWM_3.SetPWM(pinMap.LDB, OUT_LDB_Start, OUT_LDB_Stop);
}

void Reactor::setBlueIndicator(bool state) {
    blue_indicator_state = state;
    heatboard_io_chip->digitalPinWrite(pinMap.SLB, state); // Set the SLA pin to high
}

void Reactor::setOrangeIndicator(bool state) {
    orange_indicator_state = state;
    heatboard_io_chip->digitalPinWrite(pinMap.SLA, state); // Set the SLA pin to high
}

////////////////// IR ///////////////////

bool Reactor::getIRIO() {
    //  return the current IR state
    return IRState;
}
void Reactor::setIRIO(bool state) {
    IRState = state;    
    teensy_device.sendDebug("Set IRIO to %s", state ? "ON" : "OFF");
    // This is the function to turn on the IR LED. Note it also needs SYNC enabled for that channel to work
    if (state) {
        CHIPS.Senseboard_IO_1.digitalPinWrite(pinMap.DF, 1);
    } else {
        CHIPS.Senseboard_IO_1.digitalPinWrite(pinMap.DF, 0);
    }
}

bool Reactor::getSYNC() {
    // return the current SYNC state
    return SYNCState;
}

void Reactor::enableSYNC(bool state) {
    SYNCState = state;    
    teensy_device.sendDebug("Sync to %s", state ? "ON" : "OFF");

    //Here we should have alreayd set up the IR so we just need to turn on the PWM to half level
    if (state){
        analogWrite(pinMap.IR_SYNC, 2048);  // Set the SYNC signal to be 50% duty cycle
    } else {
        analogWrite(pinMap.IR_SYNC, 0);  // Set the SYNC signal to be 0% duty cycle
    }
}

void Reactor::setIRWavelength(uint16_t wavelength) {
    IRWavelength = wavelength;
    teensy_device.sendDebug("Setting wavelength to %d", wavelength);
    // Could change this to use an enum that sets both DA and DB at same time to reduce i2c calls but shouldnt get called that often so doesnt really matter
    if (wavelength == 0) {
        CHIPS.Senseboard_IO_1.digitalPinWrite(pinMap.DA, 0);
        CHIPS.Senseboard_IO_1.digitalPinWrite(pinMap.DB, 0);
        // Turn off IR LED
    } else if (wavelength == 850) {
        CHIPS.Senseboard_IO_1.digitalPinWrite(pinMap.DA, 1);
        CHIPS.Senseboard_IO_1.digitalPinWrite(pinMap.DB, 1);
        // Set the IR LED to be 850nm
    } else if (wavelength == 810) {
        CHIPS.Senseboard_IO_1.digitalPinWrite(pinMap.DA, 0);
        CHIPS.Senseboard_IO_1.digitalPinWrite(pinMap.DB, 1);
        // Set the IR LED to be 810nm
    } else if (wavelength == 770) {
        CHIPS.Senseboard_IO_1.digitalPinWrite(pinMap.DA, 1);
        CHIPS.Senseboard_IO_1.digitalPinWrite(pinMap.DB, 0);
        // Set the IR LED to be 770nm
    } else {
        // Print an error if none of the above states match
        teensy_device.sendError("Not a valid IR wavelength you SILLY GOOSE");

        // Serial.println("Not a valid IR wavelength - please specify between off,850nm,810nm and 770nm");
    }
}

uint16_t Reactor::getIRWavelength() {
    // Placeholder: return the current IR state
    return IRWavelength;
}

bool Reactor::isPDAdsorption() {
    // Returns what PD we are reading from
    return PD_is_adsorption;
}

void Reactor::selectAdsorptionPD(bool state) {
    allow_od_readings = false; // disable OD readings while switching PDs
    PD_is_adsorption = state;
    if (PD_is_adsorption){
        CHIPS.Senseboard_IO_1.digitalPinWrite(pinMap.DC, 0);
        // teensy_device.sendError("Reactor %d PD Selector: Adsorption", reactor_name);
    } else {
        CHIPS.Senseboard_IO_1.digitalPinWrite(pinMap.DC, 1);
        // teensy_device.sendError("Reactor %d PD Selector: Scatter", reactor_name);
    }
    scatter_adsorption_switch_delay_timer = 0; // prevents saving any readings from the ADC that have readings from both PDs mixed
    first_reading_after_pd_switch = true;
    allow_od_readings = true; // re-enable OD readings

}

bool Reactor::isPDFilterFast() {
    // return the current PD filter state
    return ODFilterIsFastMode;
}

void Reactor::setPDFilterFastMode(bool state) {
    ODFilterIsFastMode = state; // Store the updated state of the filter
    
    if (ODFilterIsFastMode) { 
        teensy_device.sendDebug("%d Filter: ModeA, Sinc5, Type 1 (Fast)", reactor_name);
        CHIPS.Senseboard_ADC.SetChannelMode(pinMap.ADC_Channel, AD7768::ChannelMode::MODE_A); //Set the Channel that we are using to be the fast readout mode, ADC filter type 1
    } else {
        teensy_device.sendDebug("%d Filter: ModeB, Wideband, Type 0 (Low Noise)", reactor_name);
        CHIPS.Senseboard_ADC.SetChannelMode(pinMap.ADC_Channel, AD7768::ChannelMode::MODE_B); //Set the Channel that we are using to be the low noise mode, ADC filter type 0
    }
}


// Activates the pid loop in the heater interrupt for this reactor, the heater duty cycle is set within the interrupt pid loop
void Reactor::setPIDIO(bool state){
    HeaterPIDState = state;
    // As an extra safety measure if cuvette heater PID is set to off explicitly set to 0 duty cycle
    if (state == false){
        setCuvetteHeaterDutyCycle(0);
        // Set PID terms to 0 for reporting purposes
        p_term = 0;
        i_term = 0;
        d_term = 0;
        new_duty_cycle = 0;
    }

}

bool Reactor::getPIDIO(){
    return HeaterPIDState;
}

float Reactor::getTargetTemp(){
    return requested_cuvette_temp;
}

void Reactor::setTargetTemp(float target_temp){
      
    requested_cuvette_temp = target_temp;

    // reset all terms
    p_term = 0;
    i_term = 0;
    d_term = 0;
    
    last_measured_temperature = getCuvetteTemp(); 
    // estimated_duty_cycle_to_reach_setpoint = ((requested_cuvette_temp - 23.0) * 143.1) - 542.63;

    if (requested_cuvette_temp > 0) {
        setPIDIO(true); // Turn on the heater
    } else {
        setPIDIO(false); // Turn off the heater
    }

    // reached_temp_threshold = false; // Reset the reached setpoint flag

    // if (estimated_duty_cycle_to_reach_setpoint > 4096) {
    //     teensy_device.sendInfo("Reactor " + reactor_name + " requested temperature may not be able to be reached");
    //     estimated_duty_cycle_to_reach_setpoint = 4096; // Limit the duty cycle to a maximum of 100%
    // } else if (estimated_duty_cycle_to_reach_setpoint < 0) {
    //     estimated_duty_cycle_to_reach_setpoint = 0; // Limit the duty cycle to a minimum of 0%
    // }

    teensy_device.sendDebug("Setting target temperature to: %.2f °C", requested_cuvette_temp);
}

void Reactor::setCuvetteHeaterDutyCycle(uint16_t duty_cycle){
    if (duty_cycle > 0) {
        setOrangeIndicator(true);
    } else {
        setOrangeIndicator(false);
    }
    CHIPS.Heatboard_PWM_1.SetPWM(pinMap.INN, duty_cycle);
    teensy_device.sendDebug("Reactor heater %d pin: %d duty: %d", reactor_name, pinMap.INN, duty_cycle);
    CuvetterHeaterDutyCycle = duty_cycle;
}

uint16_t Reactor::getCuvetteHeaterDutyCycle(){
    return CuvetterHeaterDutyCycle;
}
    
void Reactor::updateCuvetteTemp(float temp){
    // Note this function is called by the machine object when it has a new RTD measurement that the reactor may use, and we send it in to the reactor.
    // Theoretically this should be getting updated approximately once every 10 seconds for reach reactor by setReactorCuvetteTemp inside machine class.
    cuvetteTemp = temp;
}

float Reactor::getCuvetteTemp(){
    return cuvetteTemp;
}

// updates the heater duty cycle using PID to match the set temperature, this is run every time the ReactorHeaterTimerFunc is called and the reactor heater IO is set to on
void Reactor::HeaterPIDLoop(){
    // float temp_threshold = 1.5; // threshold for when to start PID control
    float measured_temperature = getCuvetteTemp(); // Get the current temperature
    
    float i_term_old = i_term; // Store the old integral term for the derivative calculation and anti windup

    error = requested_cuvette_temp - measured_temperature;

    p_term = kp * error; // Proportional term
    i_term = i_term_old + ki * error * dt;
    d_term = -kd * (measured_temperature - last_measured_temperature) / dt; // Derivative term

    new_duty_cycle = p_term + i_term + d_term;;

    if (new_duty_cycle >= 4096) {
        new_duty_cycle = 4096; // Limit the duty cycle to a maximum of 100%
        i_term = i_term_old; // prevent windup
    } else if (new_duty_cycle <= 0) {
        new_duty_cycle = 0; // Limit the duty cycle to a minimum of 0%
        i_term = i_term_old; // prevent windup
    }

    last_measured_temperature = measured_temperature; // Store the last measured temperature for the next iteration
   
    setCuvetteHeaterDutyCycle(new_duty_cycle);

    if (abs(error) < 0.1){ // if the temperature is within 0.1 degC we have reached the set point
        setBlueIndicator(true); 
    } else {
        setBlueIndicator(false);
    }

    teensy_device.sendDebug("Reactor: %d Duty: %d Temp: %.2f/%.2f°C", reactor_name, new_duty_cycle, cuvetteTemp, requested_cuvette_temp);
    
}

AD7768Data Reactor::getODData() {
    // Reactor AL = Channel 7
    // Reactor AR = Channel 6
    // Reactor BL = Channel 5
    // Reactor BR = Channel 4
    // Reactor CL = Channel 0
    // Reactor CR = Channel 1
    // Reactor DL = Channel 2
    // Reactor DR = Channel 3
    AD7768Data data = CHIPS.Senseboard_ADC.getADCData()[pinMap.ADC_Channel];
    data.voltage = float(data.value) * 1.25 / 8388607.0; // For this specific ADC, VRef- is VM and VRef+ is VH, so the 1.25 value is defined by the difference between VM and VH which is set in Machine::generalconfig
    return data;
}


double Reactor::getPressureReg(int pressure_regulator_id) {
    // Placeholder: return the current pressure regulator value
    if (pressure_regulator_id == 1) {
        return 1.5;
    } else if (pressure_regulator_id == 2) {
        return 2.5;
    } else {
        return 0;
    }
}