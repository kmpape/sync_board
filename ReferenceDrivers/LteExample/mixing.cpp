#include "mixing.h"


Mixing::Mixing() : mixing_name(MIXING_BOTTLE::NONE) {
    teensy_device.sendError("Invalid system name provided for Mixing constructor.");
}

Mixing::Mixing(MIXING_BOTTLE::Type name) : mixing_name(name) {
    setPinMap();
}

void Mixing::setToDefault(){
    HeaterPIDState = false; //default is false
    pid_packet_id = 0;
    send_pid_packets = false;

    mixingTemp = -1.0;
    heater_duty_12bit = 0;

    p_term = 0.0;
    i_term = 0.0;
    d_term = 0.0;
    error = 0.0;
}

void Mixing::setPinMap() {
    // Example of setting pinMap, you should fill this with actual data
    // Add more mappings as needed
    if (mixing_name == MIXING_BOTTLE::A) {
        pinMap.ADC_Channel = 0;
        pinMap.Thermo_Mux_PIN  = 0; // pin is the channel
        pinMap.HEAT_1_PIN = 15;
        pinMap.HEAT_2_PIN = 16;

    } else if (mixing_name == MIXING_BOTTLE::B) {
        pinMap.ADC_Channel = 1;
        pinMap.Thermo_Mux_PIN  = 1; // pin is the channel
        pinMap.HEAT_1_PIN = 17;
        pinMap.HEAT_2_PIN = 18;

    } else if (mixing_name == MIXING_BOTTLE::C) {
        pinMap.ADC_Channel = 2;
        pinMap.Thermo_Mux_PIN  = 2; // pin is the channel
        pinMap.HEAT_1_PIN = 19;
        pinMap.HEAT_2_PIN = 20;

    } else if (mixing_name == MIXING_BOTTLE::D) {
        pinMap.ADC_Channel = 3;
        pinMap.Thermo_Mux_PIN  = 3;
        pinMap.HEAT_1_PIN = 21;
        pinMap.HEAT_2_PIN = 22;
         // pin is the channel
    }
}


AD7768Data Mixing::getLoadCellData() {
    // Topboard ADC channels:
    // Channel 0 - Mixing A Load Cell
    // Channel 1 - Mixing B Load Cell
    // Channel 2 - Mixing C Load Cell
    // Channel 3 - Mixing D Load Cell
    // Channel 4 - Weigh Bottle Load Cell
    // Channel 5 - Current Sensing for Mixing Bottle Heaters
    // Channel 6 - Liquid Sensing 
    // Channel 7 - not used
    AD7768Data data = CHIPS.Topboard_ADC.getADCData()[pinMap.ADC_Channel];
    if (!data.filter_not_settled){ // only report voltages that are valid after startup settling
        data.voltage = float(data.value) * 1.33 / 8388607.0;
    } else { // set to a recognisably incorrect value in case of filter not settled or repeated data
        data.voltage = -99.0;
    }


    return data;
}

void Mixing::setPIDIO(bool state) {
    HeaterPIDState = state;
    // As an extra safety measure if disabling heater pid control, set the duty cycle to be 0
    if (state == false){
        // Set the heater duty cycle to 0
        setHeaterDuty(0);
        // Set PID terms to 0 for reporting purposes
        p_term = 0;
        i_term = 0;
        d_term = 0;
        new_duty_cycle = 0;
    }
}

bool Mixing::getPIDIO(){
    return HeaterPIDState;
}

void Mixing::setHeaterDuty(uint16_t heater_duty_cycle_12bit){
    
    CHIPS.Topboard_PWM_2.SetPWM(pinMap.HEAT_1_PIN, heater_duty_cycle_12bit);
    CHIPS.Topboard_PWM_2.SetPWM(pinMap.HEAT_2_PIN, heater_duty_cycle_12bit);

    heater_duty_12bit = heater_duty_cycle_12bit;
}

uint16_t Mixing::getHeaterDuty(){
    return heater_duty_12bit;
}

void Mixing::updateMixingTemp(float temp) {
    // This function is used to update the temperature of the mixing bottle in celsius, this value gets updated by the othertemp loop in main.cpp
    mixingTemp = temp;
}

float Mixing::getMixingTemp() {
    // Returns the temperature of the mixing bottle in celsius, this value gets updated by the othertemp loop in main.cpp
    return mixingTemp;
}

float Mixing::getTargetTemp(){
    return requested_mixing_temp;
}

void Mixing::setTargetTemp(float target_temp){
      
    requested_mixing_temp = target_temp;
    // reset all terms
    p_term = 0;
    i_term = 0;
    d_term = 0;
    
    last_measured_temperature = getMixingTemp(); 
    
    if (requested_mixing_temp > 0) {
        setPIDIO(true); // Turn on the heater
    } else {
        setPIDIO(false); // Turn off the heater
    }

    teensy_device.sendDebug("Setting target temperature to: %.2f °C", requested_mixing_temp);
}

void Mixing::HeaterPIDLoop(){

    float measured_temperature = getMixingTemp(); // Get the current temperature

    float i_term_old = i_term; // Store the old integral term for the derivative calculation and anti windup

    error = requested_mixing_temp - measured_temperature;

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
   
    setHeaterDuty(new_duty_cycle);

    teensy_device.sendDebug("Mixing Bottle: %d Duty: %d Temp: %.2f/%.2f°C", (int)mixing_name, new_duty_cycle, measured_temperature, requested_mixing_temp);
    
}