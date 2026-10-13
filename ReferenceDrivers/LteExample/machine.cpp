#include "machine.h"


Machine::Machine(){
}

void Machine::generalConfig(){
    //This function does the general configuration of the machine. It should be called when you enable system. It does things that are not reactor-specific.
    
    // VaRIOUS PWM SETUPS
    analogWriteResolution(12); //We coud increase resoluton here should we wish it but set this to 12 bit for now. NOTE if you change this you need to go change things like enableIR() in Reactor class.
    //Note the analog write is being used to create PWM signals rather than actually any analog voltage we need :) 

    //Set up the voltage levels for the IR LED system which are controleld by Senseboard_DAC
    // If you change these values check the reactor class for getODData as they impact the conversion to voltage.
    float VL = 0.8; //Give reverse bias on photodiodes so around 1V good to reduce capacitance/increase speed.
    float VM = VL + 0.35; //Should b e at least 0.1 above VL Nice to have it a bit larger to increase integration speed when reducing LED power.
    float VH = VM + 1.25; // Must be at least 1 above VM as this is minimum reference difference on the ADC
    float VR = 1.25; //something happily mid range for the ADC
    float VCM = 1.5; //Common mode coming out of the lockin amplifier
    CHIPS.Senseboard_DAC.SetVoltage(VL_PIN, VL);  // This is VL
    CHIPS.Senseboard_DAC.SetVoltage(VM_PIN, VM);  // This is VM
    CHIPS.Senseboard_DAC.SetVoltage(VH_PIN, VH);  // This is VH
    CHIPS.Senseboard_DAC.SetVoltage(VR_PIN, VR);  // This is VR
    CHIPS.Senseboard_DAC.SetVoltage(VCM_PIN, VCM);  // This is VCM

    //RTD SETUP
    //Note these need to be active for essentially every function INCLUDING the configuration.
    CHIPS.Senseboard_IO_1.digitalPinWrite(EXCITE_ENABLE_PIN, 1); //This forces connect of RTD Excite signal - we basically always need this on to run the reference! 
    CHIPS.Senseboard_IO_1.digitalPinWrite(RTD_ENABLE_PIN, 1); // This forces connect of the RTD Measurement signal - we COULD let this go off but why bother. 

    setStirVoltage(0); //Set the stir voltage to 4.7V as default.

    //Weigh Scales Setup
    digitalWrite(Load_Cell_Chop, HIGH);  // This is the chop pin on the teensy which we arent actually using... could get rid of it  if we need.
    CHIPS.Topboard_PWM_1.SetFrequency(112.5, 1843200); // 112.5 should be the maximum frequency possibel  with our slow clock assumed at 1843200.
    // I.e. we are dividng clock down by 16384

    CHIPS.Topboard_PWM_1.SetPWM(CHOP2_PIN, 4096);       // This is Chop2 on the schematic - i.e. we tell it we DO want to chop after signal amplifiraction.
    CHIPS.Topboard_PWM_1.SetPWM(CONF1_PIN, 4096);       // This is Conf1 on schematic - default to high which does something on the switcher...
    CHIPS.Topboard_PWM_1.SetPWM(CONF2_PIN, 4096);       // This is Conf2 on schematic - default to high  which does something on the switcher...
    CHIPS.Topboard_PWM_1.SetPWM(CONF3_PIN, 0);          // This is Conf3 on schematic - default to low which sets the switching speed of vvoltage switch.
    CHIPS.Topboard_PWM_1.SetPWM(CONF4_PIN, 0);          // This is Conf4 on schematic - default to low which also sets the switching speed of vvoltage switch.
    CHIPS.Topboard_PWM_1.SetPWM(CHOP0_PIN, 2048);       // This is Chop0 on schematic - which we set to 50 percent duty cycle which is the default for the load cell half time in each state.
    CHIPS.Topboard_PWM_1.SetPWM(VBRIDGE_EN_PIN, 4096);   // This is VBridgeEN on the schematic - turnin gon the 8V power supply to the bridges.
    CHIPS.Topboard_PWM_1.SetPWM(FLUIDSENSE_EN_PIN, 4096); // This enables the 5V supply to the fluid sensors

    setFluidSensorState(true); // Enable the fluid sensor by default. This is done in the general config so that it is not enabled when the system is first turned on.

    for (uint8_t i=0; i<REACTOR_COUNT; i++){
        reactors[i].setToDefault();
    }
    for (uint8_t i=0; i<MIXING_BOTTLE_COUNT; i++){
        mixing_bottles[i].setToDefault();
    }

}

void Machine::setStirVoltage(uint8_t level){
    // Default for the chip will be 4.7V, as in this is what it is set to before this command is called for the first time.
    if (level == 0){            // 4.7V
        CHIPS.Heatboard_IO_4.digitalPinWrite(VS0_PIN, 1);
        CHIPS.Heatboard_IO_4.digitalPinWrite(VS1_PIN, 0);
    } else if (level == 1) {    // 6V
        CHIPS.Heatboard_IO_4.digitalPinWrite(VS0_PIN, 0);
        CHIPS.Heatboard_IO_4.digitalPinWrite(VS1_PIN, 1);
    } else if (level == 2) {    // 8V
        CHIPS.Heatboard_IO_4.digitalPinWrite(VS0_PIN, 1);
        CHIPS.Heatboard_IO_4.digitalPinWrite(VS1_PIN, 1);
    } else {
        teensy_device.sendError("Invalid stir voltage level, choose from 0 (4.7V), 1 (6V), or 2 (8V)");
    }
}

void Machine::setValveState(MIXING_BOTTLE::Type system_int, uint32_t state) {
    // This is now as efficient as possible, updating only the banks and chips that have been altered

    
    // a mask that it only true for bits that are different between valve_state and state
    uint32_t diff_mask = valve_state[system_int] ^ state;

    /*
    diff_mask 32 bit number that tells which valves need updating
    find the chip, bank, and pin for each valve that needs updating 
    how do we programmatically keep record of the chip that it correpsonds to?
    bool iostate[4][5][8] = {false};
    */
    PCA9505* valve_chips[4] = {&CHIPS.Heatboard_IO_1, &CHIPS.Heatboard_IO_2, &CHIPS.Heatboard_IO_3, &CHIPS.Heatboard_IO_4};
    PCA9505* indicator_chips[2] = {&CHIPS.Topboard_IO_1, &CHIPS.Topboard_IO_2};

    uint8_t io_state[4][5]          = {{0b00000000}}; // could just put 0 but put the binary 8 bits to make super clear what is happening here
    uint8_t io_update_mask[4][5]    = {{0b00000000}};

    uint8_t indicator_state[2][5]          = {{0b00000000}};
    uint8_t indicator_update_mask[2][5]    = {{0b00000000}};

    // First populate io_state and io_update_mask to keep a record of which bits we are changing 
    for (uint8_t i = 0; i < VALVE_COUNT; i++) {
        if ((diff_mask >> i) & 1) {
            bool individual_valve_state = (state >> i) & 1;
            
            io_update_mask[(valveMap[system_int][i].chip - 1)][valveMap[system_int][i].bank] |= (uint8_t(1) << valveMap[system_int][i].position);
            io_state[(valveMap[system_int][i].chip - 1)][valveMap[system_int][i].bank] |= (individual_valve_state << valveMap[system_int][i].position);

            indicator_update_mask[(indicatorMap[system_int][i].chip - 1)][indicatorMap[system_int][i].bank] |= (uint8_t(1) << indicatorMap[system_int][i].position);
            indicator_state[(indicatorMap[system_int][i].chip - 1)][indicatorMap[system_int][i].bank]       |= (individual_valve_state << indicatorMap[system_int][i].position);
        }
    }

    // Then we use these arrays to update the entire bank at once for the valves
    for (int chip = 0; chip < 4; chip++) {
        for (int bank = 0; bank < 5; bank++) {
            if (io_update_mask[chip][bank] != 0) { // check if any bits need updating in this bank
                uint8_t local_data = valve_chips[chip]->getLocalBankData(bank);

                local_data &= ~io_update_mask[chip][bank];  // Clear the bits that need updating
                local_data |= io_state[chip][bank];         // Set the bits to their new state, io_state will be 0 for bits we are not changing so it is safe to use OR

                valve_chips[chip]->digitalBankWrite(bank, local_data);
            }
        }
    }

    // Then we do the same for the indicator leds
    for (int chip = 0; chip < 2; chip++) {
        for (int bank = 0; bank < 5; bank++) {
            if (indicator_update_mask[chip][bank] != 0) { // check if any bits need updating in this bank
                uint8_t local_data = indicator_chips[chip]->getLocalBankData(bank);

                local_data &= ~indicator_update_mask[chip][bank];  // Clear the bits that need updating
                local_data |= indicator_state[chip][bank];         // Set the bits to their new state, indicator_state will be 0 for bits we are not changing so it is safe to use OR
                
                if (machine.enable_valve_leds) {
                    indicator_chips[chip]->digitalBankWrite(bank, local_data);
                }
            }
        }
    }

    // Update the state of the other letters for the shared valves
    for (uint8_t i = 0; i < MIXING_BOTTLE_COUNT; i++) {
        for (uint8_t j = SHARED_VALVE_START_INDEX; j < VALVE_COUNT; j++) {
            bool individual_valve_state = (state >> j) & 1;
            valve_state[i] &= ~(uint32_t(1) << j); // Clear the bit for this valve
            valve_state[i] |= (individual_valve_state << j); // Set the bit to the new state
        }
        // teensy_device.sendError("Letter %d: state: %d", i, valve_state[i]); 
    }
    // Update the desired letter with the full state
    valve_state[system_int] = state;
    // teensy_device.sendError("Letter %d: state: %d", system_int, state); 
}

uint32_t Machine::getValveState(uint8_t system_int) {
    return valve_state[system_int];
}

void Machine::setFanDuty(uint16_t fan_duty_cycle) {
    fanDutyCycle = fan_duty_cycle; // update value stored on machine
    // Update the hardware with the new fan duty cycle
    CHIPS.Heatboard_PWM_1.SetPWM(FAN_PIN, fanDutyCycle); // Assuming 0 is the pin for the fan
}

uint16_t Machine::getFanDuty() {
    return fanDutyCycle;
}

void Machine::setPeristalticDuty(uint16_t peristaltic_duty_cycle) {
    peristalticDutyCycle = peristaltic_duty_cycle; // update value stored on machine
    // Update the hardware with the new peristaltic duty cycle
    CHIPS.Heatboard_PWM_1.SetPWM(PERISTALTIC_PIN, peristalticDutyCycle); // Assuming 0 is the pin for the fan
}

uint16_t Machine::getPeristalticDuty() {
    return peristalticDutyCycle;
}

void Machine::setPolarityChannelRTDADC(int channel, bool polarity){
    // Function that is used to chose the channel ( from 0 to 7) and polarity (two ways) of the RTD.
    //The reason we have this here instead of inside the ADS124S08 class is that it needs to send comands elsewhere e.g. through SPI to the AD7768 on senseboard.
    
    // We need to create a binary number which is 0,0,0,0,RTD_Polarity,MUX2,MUX1,MUX0
    // The MUX bits are used to select the channel and the RTD_Polarity is used to select the polarity of the RTD.
    uint8_t MUX = channel; // This is the channel we want to read from. It indexes from 0 in datasheet of the ADG707 (Table II)
    uint8_t RTD_Polarity = polarity; // This is the polarity we want to read from - this flips the orientation or excitation to the thingo.
    uint8_t command = 0b00000000 | (RTD_Polarity << 3) | MUX; // This is the command we want to send to the ADC.
    CHIPS.Senseboard_ADC.SetGPIOMode(command); // This is the command we send to the ADC.

    //Here we are doing an external recofnigure of the ADC but not actually messing with its config registers so we do need to get it to restart conversions
}

void Machine::setReactorCuvetteTemp(int channel, float temp){
    // This used to assign the recent temperature reading to whatever reactor it is associated with.

    if (channel == 0){
        reactors[REACTOR::AL].updateCuvetteTemp(temp);
    } else if (channel == 1){
        reactors[REACTOR::AR].updateCuvetteTemp(temp);
    } else if (channel == 2){
        reactors[REACTOR::BL].updateCuvetteTemp(temp);
    } else if (channel == 3){
        reactors[REACTOR::BR].updateCuvetteTemp(temp);
    } else if (channel == 4){
        reactors[REACTOR::CL].updateCuvetteTemp(temp);
    } else if (channel == 5){
        reactors[REACTOR::CR].updateCuvetteTemp(temp);
    } else if (channel == 6){
        reactors[REACTOR::DL].updateCuvetteTemp(temp);
    } else if (channel == 7){
        reactors[REACTOR::DR].updateCuvetteTemp(temp);
    } 
}

float Machine::getLEDCurrent(){
    return LEDCurrent; //Units of mA
}

void Machine::setPressure_MPa(float requested_pressure, int regulator_select){
    // Pressure is between 0.005-0.1 MPa
    // unable to set outside of the rated range, so any pressure below 0.005Mpa gets set to 0.005MPa
    PresVoltConversion result = pressure_to_voltage(requested_pressure);

    if (regulator_select == 1) {
        // teensy_device.sendInfo("Pressure1 set to: %.3f MPa, Voltage: %.3f", result.pressure, result.voltage);
        CHIPS.Senseboard_DAC.SetVoltage(PREG1_PIN, result.voltage);
    } else if (regulator_select == 2) {
        // teensy_device.sendInfo("Pressure2 set to: %.3f MPa, Voltage: %.3f", result.pressure, result.voltage);
        CHIPS.Senseboard_DAC.SetVoltage(PREG2_PIN, result.voltage);
    } else {
        teensy_device.sendError("Invalid pressure regulator select. Choose from 1 or 2");
    }
}

Machine::PresVoltConversion Machine::pressure_to_voltage(float requested_pressure){
    PresVoltConversion output;

    if (requested_pressure > max_pressure_MPa){
        requested_pressure = max_pressure_MPa;
    } else if (requested_pressure < min_pressure_MPa){
        requested_pressure = min_pressure_MPa;
    }

    float M = (requested_pressure - Pmin) / (Pmax - Pmin);
        
    input_pressure_voltage = ((max_voltage_reg_input - min_voltage_reg_input) * M) + min_voltage_reg_input;

    if (input_pressure_voltage > max_voltage_dac_output){
        teensy_device.sendError("%.3f MPa requires %.3fV. Max Poss = %.3fV", requested_pressure, input_pressure_voltage, max_voltage_dac_output);
        input_pressure_voltage = max_voltage_dac_output;
    } else if (input_pressure_voltage < min_voltage_dac_output){
        teensy_device.sendError("%.3f MPa requires %.3fV. Min Poss = %.3fV", requested_pressure, input_pressure_voltage, min_voltage_dac_output);
        input_pressure_voltage = min_voltage_dac_output;
    }

    applied_pressure = voltage_to_pressure_input(input_pressure_voltage);
    
    output.pressure = applied_pressure;
    output.voltage = input_pressure_voltage;

    return output;
}

float Machine::voltage_to_pressure_input(float requested_voltage){
    // input refers to the voltage input to the pressure regulator, the setpoint voltage
    if (requested_voltage > max_voltage_dac_output){
        requested_voltage = max_voltage_dac_output;
    } else if (requested_voltage < min_voltage_dac_output){
        requested_voltage = min_voltage_dac_output;
    }

    float M = (requested_voltage - min_voltage_reg_input) / (max_voltage_reg_input - min_voltage_reg_input);

    float pressure = (M * (Pmax - Pmin)) + Pmin;

    return pressure;
}

float Machine::voltage_to_pressure_output(float output_voltage){
    // output refers to the voltage output from the pressure regulator, the monitor voltage
    
    float M = (output_voltage - min_voltage_reg_output) / (max_voltage_reg_output - min_voltage_reg_output);
    // float M = (output_voltage - 1.0) / (5 - 1.0);

    float pressure = M * max_pressure_MPa;

    return pressure;
}

float Machine::getPressure1(){
    return Pressure1_MPa; //Units of something - will depend on pressure regulator
}

float Machine::getPressure2(){
    return Pressure2_MPa; //Units of something
}

void Machine::processRTDADCFastread(int fastreadtype, float adc_raw_voltage){
    if (fastreadtype==1){ // LED Current
        LEDCurrent = 1000.0*adc_raw_voltage/(0.01*20.0*8.0); // 0.01 factor is 10mOhm sense resistor, and 20 is the gain of the current sense amplifier. factor 8.0 is because we are using 8X PGA on ADC for this mode. 1000 convert to mA

    } else if (fastreadtype == 2){ // Pressure Sensor 1
        // the raw voltage from the ADC is in the range of 1.0 to 5.0V corresponding to 0.0 to 0.1 MPa
        // this voltage then gets halved by the circuitry so we have a voltage being read by the ADC between 0.5V and 2.5V
        // when multiplying the adc_raw_voltage by 5.0 we convert this raw value to what is directly output from the regulator
        // the 0.04 is a bit of a fudge factor, there must be some resistance somewhere causing an extra drop
        // the 1.0003 and 0.0007 are also fudge factors found by shorting the ADC to the DAC and finding the relationship between the readings
        // teensy_device.sendInfo("ADC Raw Voltage Pressure1: %.4f V", ((adc_raw_voltage*5.0)*1.0003)+0.0007+0.04);
        Pressure1_MPa = voltage_to_pressure_output(((adc_raw_voltage*5.0)*1.0003)+0.0007+0.04); 
    } else if (fastreadtype == 3){ // Pressure Sensor 2
        // teensy_device.sendInfo("ADC Raw Voltage Pressure2: %.4f V", ((adc_raw_voltage*5.0)*1.0003)+0.0007+0.04);
        Pressure2_MPa = voltage_to_pressure_output(((adc_raw_voltage*5.0)*1.0003)+0.0007+0.04);
    }

}

void Machine::runRTDADC(bool calibrate, bool fastread, int fastreadType){
    // This is function which is run every so often (i.e. at least at 10hz) by the the main loop and it is used to decide what this RTD ADC on Senseboard should be doing.
    // You can basically call it from anywhere at any time and it SHOULD prioritise things correctly. You do need to repeatedly call it to ensure stuff works.
    // Admittedly it is a bit of a messy state machine but if it works it works :) 
    

    // Calibrate is a logic flag to see if we are asking it to do a calibration
    // fastread is a logic flag to see if we are asking it to do a fast readout
    // fastreadType is an integer that tells us what we are asking it to do a fast readout of. 0 is nothing/error, 1 is LED current, 2 is pressure sensor 1, 3 is pressure sensor 2. 
    
    if (calibrate || RTDADC_Calibrating){ //Everything that we need to handle when doing ADC calibration - note this is the highest priority thing and should preventother ADC operations.
        // Idea here is you run this first with calibrate=true then it will set RTDADC_Calibrating true and keep doing this process until it is done.
        
        if (calibrate == true && RTDADC_Calibrating==false && RTDADC_calibrationStep == 0){// In this case we need to start calibrating aand is only going to arrive if calibrate==true
            // First we turn off the RTDs- we don't THINK this is important really but just do it for sanity sake.

            // We are about to start calibrating, but the RTD might be happening - so we need to shut this down safely in that case.
            if (RTDADC_RTDReading == true){ // i.e. if we ARE indeed dealing with the RTD.
                RTDADC_RTDForceShutdown = true; // Set the status of the RTD to failed - indicating that it WAS reading this one but got unhappy so we need to repeat.
                RTDADC_RTDReading = false; // Reset the flag to say we are now NOT reading the RTD e.g. because we suck and it broke. 
            }
            setPolarityChannelRTDADC(0, true); //Connnect RTD 0 - note that we need to do this (connect an RTD) to ensure current passes through the buffer resistor and hence the whole system to run the configuration part.

            RTDADC_calibrationStep = 0; //Advance to first calibration step. Note we do this here instead of inside calibrate() to avoid... some possible errors.
            RTDADC_Calibrating = true; //Set the flag to say we are calibrating.
            RTDADC_commandStarted = millis(); //Set the time that we started the command to now.
            RTDADC_calibrationStep=CHIPS.Senseboard_ADC2.calibrate(RTDADC_calibrationStep); //Run the calibration function.

  
        } else if (RTDADC_Calibrating && millis()-RTDADC_commandStarted > RTDADC_calibrationTimeout){// This is it having run enough time to complete what it was doing..
            RTDADC_commandStarted = millis(); //Set the time that we started the command to now.
            RTDADC_calibrationStep=CHIPS.Senseboard_ADC2.calibrate(RTDADC_calibrationStep); //Run the calibration function.
            if (RTDADC_calibrationStep==-1){//In this case we have reacehed the end.
                RTDADC_calibrationStep = 0; //Reset the calibration step to 0.
                RTDADC_Calibrating = false; //Reset the flag to say we are calibrating.
                // Now we are finished calibrating we can turn back on the RTDs.Note we COULD put this at teh start of reach RTD Reading but it is bit unnecessary and adds I2C traffic.
                teensy_device.sendInfo("RTD ADC calibrated at: %.7f", CHIPS.Senseboard_ADC2.getCalibrationFactor());
                RTDADC_Calibrated = true; //Set the flag to say we have calibrated at least once.

            }
              
        } else {
            // This case would activate if you are calibrating but it is not at the end nor is it time to do anything, hence we continue to wait.
        }
    
    } else if (fastread || RTDADC_Fastreading) { // called when we want to read somethign immediately, or if we are currently doing that.

        if (RTDADC_Fastreading && fastread) { 
            // THis could happen if it is already fastreading then it is instructed to to fastread something else again.
            teensy_device.sendInfo("RTD ADC already fastreading - wait!");
        
        } else if (RTDADC_Fastreading == false && fastread == true){ //So only if we are starting up the fastread.
            // We are about to start reconfiguring the device. In case the RTD read is happening (which it may well be) we shut it down safely.
            if (RTDADC_RTDReading == true){ // i.e. if we ARE already running the RTD
                RTDADC_RTDForceShutdown = true; // Set the status of the RTD to failed - indicating that it WAS reading this one but got unhappy so we need to repeat.
                RTDADC_RTDReading = false; // Reset the flag to say we are now NOT reading the RTD e.g. because we suck and it broke. 
            }
            RTDADC_Fastreading = true; //Set the flag to say we are doing a fast readout.
            RTDADC_fastreadWhich = fastreadType; //Note we need to store this temporarily ue to the fact that runRTDADC is regularly called with no argument in the main loop and that will be the call that reaches end of this fastread process
            
            if (fastreadType == 1){
                CHIPS.Senseboard_ADC2.configure(ADS124S08_MODE::LED_CURRENT); // Set up configuration in LED current mode.
                CHIPS.Senseboard_ADC2.setMultiplexer(ADS124S08_MODE::LED_CURRENT); //Set multiplexer correcty.
                setPolarityChannelRTDADC(0, true); //Connnect RTD 0 - note that we need to do this (connect an RTD) to ensure current passes through the resistor used to generate voltage reference
                //THis is a bit of an anomoyly for the current sensor channel, we could instead configure it to use the ADC internal ref if desired...

                teensy_device.sendDebug("Starting fastread of LED current");

            } else if (fastreadType == 2){ //Pressure 1
                CHIPS.Senseboard_ADC2.configure(ADS124S08_MODE::PRESSURE_1); // Set up configuration in Pressure Reg 1 mode
                CHIPS.Senseboard_ADC2.setMultiplexer(ADS124S08_MODE::PRESSURE_1); //Set multiplexer correcty.
            } else if (fastreadType == 3){ //Pressure 2
                CHIPS.Senseboard_ADC2.configure(ADS124S08_MODE::PRESSURE_2); // Set up configuration in Pressure Reg 2 mode
                CHIPS.Senseboard_ADC2.setMultiplexer(ADS124S08_MODE::PRESSURE_2); //Set multiplexer correcty.
            } else {
                //This is an error case - we should never get here.
            }

            CHIPS.Senseboard_ADC2.startConversion(); //Start the RTD conversion.
            RTDADC_commandStarted = millis(); //Set the time that we started the command to now.

            // takes 20.2 ms to convert led current and pressure reg readings
        } else if (RTDADC_Fastreading && millis()-RTDADC_commandStarted > RTDADC_fastreadTimeout_ms){// This is it having run enough time to complete what it was doing..
            float value = CHIPS.Senseboard_ADC2.readADC();
            // teensy_device.sendInfo("Fastread ADC Val: %.3f retrieved after %d ms", value, millis()-RTDADC_commandStarted);
            processRTDADCFastread(RTDADC_fastreadWhich, value); //Set the fastread value to the right thing.
            RTDADC_Fastreading = false; //Reset the flag to say we are doing a fast readout.
            RTDADC_fastreadWhich = -1; //Set the fastread type to the right thing.

            //Read back ADC value and assign it to something useful.

        } else {
            // This case would activate if you are fastreading but it is not at the end nor is it time to do anything, hence we continue to wait.
        }

    } else { // In this case we want to handle the RTD stuff generally which is the default state of what this RTD is doing...
        if (RTDADC_RTDForceShutdown || RTDADC_RTDReading == false){ //This will be true if we arent yet reading, or if one of the other functions (calibration or fastread) has forced the RTD to shut down and we need to restart it.
            // Note this basically implies the RTD should ALWAYS be reading, and if it isn't start it!
            // Configure the RTDADC to be in right mode for that reading and start reading.
            RTDADC_RTDForceShutdown = false; //Reset the flag to say we are not forcing the RTD to shut down.
            RTDADC_RTDReading = true; //Set the flag to say we are reading the RTD.
            RTDADC_RTDStep = 0; // Note this should alreayd by 0 but we reset it to 0 anyway in case we had the forceshutdown interrupt it.
            bool RTDPolarity = (RTDADC_RTDStep+1) % 2; // Stores what polarity we want to set for the RTD. The +1 here is to get the right starting state...

            // TODO what order should this be in, should we set the OD ADC gpio first and then configure the RTD ADC or the other way around
            CHIPS.Senseboard_ADC2.configure(ADS124S08_MODE::RTD); // Set up configuration in RTD mode.
            CHIPS.Senseboard_ADC2.setMultiplexer(ADS124S08_MODE::RTD); //Set multiplexer correcty.
            setPolarityChannelRTDADC(RTDADC_RTDIndex, RTDPolarity); //Set the polarity and multiplexer for RTD.

            CHIPS.Senseboard_ADC2.startConversion(); //Start the RTD conversion.

            RTDADC_RTDStep = CHIPS.Senseboard_ADC2.readRTD(RTDADC_RTDIndex, RTDADC_RTDStep); //Run the RTD reading function with RTDstep=0 to reset things.
            //At this point we are at step = 1.
            

            RTDADC_commandStarted = millis(); //Set the time that we started the command to now.

        } else if (RTDADC_RTDReading  && millis()-RTDADC_commandStarted>RTDADC_RTDTimeout_ms){ //In this case we have finished one step for a given RTD and are on to the next.
            RTDADC_RTDStep = CHIPS.Senseboard_ADC2.readRTD(RTDADC_RTDIndex, RTDADC_RTDStep); //Tell the ADC to read the current value.
            if (RTDADC_RTDStep == -1){ // This case means we have reached the end of our steps and are done
                float newResistanceValueRatio = CHIPS.Senseboard_ADC2.getLastRTD(); //Get the last RTD value that was calculated.
                float newResistanceValue = newResistanceValueRatio * 10000; //Convert to resistance in ohms assumign that our sense resistor (R24-1) is exactly 10k.
                float R0 = 1000.0;  // resistance at 0°C
                float A = 3.9083e-3; // Coefficient A
                float B = -5.775e-7; // COefficient B
                float R_R0 = newResistanceValue / R0;   
                float newTempValue = (-A + sqrt(A*A - 4*B*(1-R_R0)))/(2*B); //Calculate the temperature from the resistance - for PT1000 RTD sensor.

                RTDReadings[RTDADC_RTDIndex] = newTempValue; //Store the RTD reading in the array of recent temperature readings. 
                setReactorCuvetteTemp(RTDADC_RTDIndex, newTempValue); //Assign the RTD value to the correct reactor.

                RTDADC_RTDIndex++;
                // IF it has reached 8 set it back to 0 to restart from first RTD
                if (RTDADC_RTDIndex == 8){
                    RTDADC_RTDIndex = 0;
                }
                RTDADC_RTDReading = false; //Reset the flag to say we are not reading the RTD.
                RTDADC_RTDStep=0; //Reset the RTD step to 0. Shouldn't TECHNICALLY be necessary since it gets reset in above case.
            } else { // In this case we are going to do a chop/reconfig.
                bool RTDPolarity = RTDADC_RTDStep % 2; // Stores what polarity we want to set for the RTD.
  

                setPolarityChannelRTDADC(RTDADC_RTDIndex, RTDPolarity); //Set the polarity and multiplexer for RTD.
                CHIPS.Senseboard_ADC2.startConversion(); //Start the RTD conversion.
            
                RTDADC_commandStarted = millis(); //Set the time that we started the command to now.
            }
        } else {
            // Do nothing - here we are basically waiting for the timer until a new reading is ready.
        }

    } 



    if (fastread && RTDADC_Calibrating){
        teensy_device.sendError("You are being naughty by asking it to do a fastread while ADC is still calibrating!");
    }

}

float Machine::getWasteLoadCellVoltage(){
    int32_t data = CHIPS.Topboard_ADC.getADCInt(WASTE_LOADCELL_ADC_CHANNEL);
    float voltage = float(data) * 1.33 / 8388607.0;
    return voltage;
}

float Machine::getMixingHeaterCurrent() {
    // Returns the total current going through the heating elements
    // Channel 5 on the ADC 
    // AD7768Data* wholedata = CHIPS.Topboard_ADC.getADCData();
    // teensy_device.sendError("c: %d | %d | c: %d | %d | %d | %d", wholedata[3].channel_ID, wholedata[3].value, wholedata[4].channel_ID, wholedata[4].value, wholedata[4].repeated_data, wholedata[4].error_flag)
    // TODO seems like no waste load cell data or mixing heater current data is coming through - why?
    int32_t data = CHIPS.Topboard_ADC.getADCInt(MIXING_HEATER_CURRENT_ADC_CHANNEL);
    float voltage = float(data) * 1.33 / 8388607.0;
    
    // voltage = current (in amps) * 0.06 according to schematic
    float current = 1000.0 * voltage / 0.06; 
    
    return current;
}

// void Machine::getFluidSensorTransition() {
//     // Enables an interrupt to be generated when fluid is detected at the requested sensor
//     // Channel 6 on the ADC 
//     // perhaps: where each entry in the struct can be Active (True), Inactive (False), automatically gets sets from Active->Inactive when triggered
//     // struct listen_for_fluid_sensor{
//     //     bool transfer_A;
//     //     bool transfer_B;
//     //     bool transfer_C;
//     //     bool transfer_D;
//     //     bool transfer_AB;
//     //     bool transfer_BC;
//     //     bool transfer_CD;
//     //     bool in_waste_A;
//     //     bool in_waste_B;
//     //     bool in_waste_C;
//     //     bool in_waste_D;
//     //     bool out_waste_A;
//     //     bool out_waste_B;
//     //     bool out_waste_C;
//     //     bool out_waste_D;
//     //     bool imb_in_waste;
//     // };

//     // when a sensor is requested to be listened to, we start updating its state similar to how we are constantly querying the RTD temperature
//     // when the value of this sensor is read to be a certain value (i.e. fluid detected), we create an interrupt that sends the value to python
//     // on the python side when this message is received it interrupts us to say a transition has been detected 
//     float voltage = getFluidSensorVoltage();
// }

float Machine::getFluidSensorVoltage(){
    int32_t data = CHIPS.Topboard_ADC.getADCInt(FLUID_SENSOR_ADC_CHANNEL);
    float voltage = float(data) * 1.33 / 8388607.0;
    return voltage;
}

void Machine::setFluidSensorState(bool state){
    // This function is used to set the state of the fluid sensor. It is used to turn on and off the fluid sensor.
    // The fluid sensor is a digital input which is pulled high when there is no fluid and low when there is fluid.
    // The state is 0 for no fluid and 1 for fluid.
    if (state != fluid_sensor_state){
        fluid_sensor_state = state;
        CHIPS.Topboard_IO_2.digitalPinWrite(LIQUID_SENSING_ENABLE_PIN, state);
    } else {
        return; // No need to change the state if it is already the same.
    }
}

bool Machine::getFluidSensorState(){
    // This function is used to get the state of the fluid sensor. It is used to turn on and off the fluid sensor.
    // The fluid sensor is a digital input which is pulled high when there is no fluid and low when there is fluid.
    // The state is 0 for no fluid and 1 for fluid.
    return fluid_sensor_state;
}


// void Machine::getFluidSensorBlanks(){
//     // This function is used to get the blanking values for the fluid sensors
//     // It will be called when the system is first started up and will be used to set the blanking values for each sensor
//     // The blanking values are used to determine if a sensor detects fluid or not
//     for (int i = 0; i < 16; i++){
//         adc_chips[Topboard_ADC"].SetGPIOMode(i);
//         delay(10); // time to settle
//         fluid_sensor_blanks[i] = adc_chips[Topboard_ADC"].getADCInt(6);
//     }
// }

//Sets the channel you want to read current from in the heatboard
void Machine::setCurrentChannel(uint8_t current_channel) {   
    // MM0 is pin 41 - bank 3 pos 4
    // MM1 is pin 40 - bank 3 pos 3
    // MM2 is pin 39 - bank 3 pos 2
    uint8_t bank_data = CHIPS.Heatboard_IO_4.getLocalBankData(CURRENT_SENSE_BANK);

    // 0 0 0 MM0 MM1 MM2 0 0
    bank_data = bank_data & 0b11100011; //Clear the MM0, MM1, MM2 bits
    
    bank_data = bank_data | (uint8_t)current_channel << CURRENT_SENSE_OFFSET;
    //Neckties 1-7 corresponding to the different rails

    CHIPS.Heatboard_IO_4.digitalBankWrite(CURRENT_SENSE_BANK, bank_data);

}

float Machine::getCurrent(){
    // readRawVoltage takes around 50ms to complete as using single shot mode on the ADC
    float current_amps;
    current_amps = 1000.0*(CHIPS.Heatboard_ADC.readRawVoltage()/20.0) / (0.003); // 1000 to convert to mA, 20x gain of the current sense amplifier, 0.003 ohm sense resistor
    //use the ohms law to calculate current 
    // Current = Voltage / Resistance
    // Resistance = 0.003 ohms
    
    return current_amps;
}
