#include <Arduino.h>
#include "PinOut.h"
#include "GlobalVariables.h"
#include "SerialController.h"
#include <SPI.h>
#include <Wire.h>
#include "digitalWriteFast.h"
#include "ICDrivers.h"
#include "IOController.h"
#include "MemoryController.h"
#include "LEDDriver.h"

//Note everything in this function should be conditional on global variable LEDAttacehd.
// Note this function never actually stores whether LEDs are off or on at a given time - this SHOULD be OK but could be added if need.

const int numLEDs = 8; //Number of LEDs on the LED board.
bool LEDOutputCurrent[numLEDs] =  {true}; //This is the state of the LED output. True means current is the output voltage the ADC can read, false is optical power.
bool LEDFeedbackCurrent[numLEDs] =  {true}; //This is the signal being used to control the LED. True means current is the feedback signal the ADC can read, false is optical power.

float LEDLevelSet[numLEDs] = {0.0}; //This is used to record whether we have set the LED level (i.e. if it is non zero)  We also use it to prevent the user from turning on the LED without setting the level first.

//These are initialised here and read from memory when needed.
float LEDTurnOnVoltageCurrent[numLEDs] = {0.0}; //This is the turn-on voltage for the LED. It is the voltage at which the LED starts to emit light.
float LEDMaxVoltageCurrent[numLEDs] = {0.0}; //This is the maximum voltage that can be applied to the LED in current mode to achieve the maximum current.
float LEDZeroReadingCurrent[numLEDs] = {0.0}; //This is the read analog value when the LED is off.
float LEDTurnOnVoltagePower[numLEDs] = {0.0}; //This is voltage at which the LED starts to emit light in optical power mode, though as above currently it is calculated from measurements in current feedback mode.
float LEDMaxVoltagePower[numLEDs] = {0.0}; //This is the maximum voltage that can be applied to the LED in optical power mode to achieve the maximum current limit.
float LEDZeroReadingPower[numLEDs] = {0.0}; //This is the read analog value when the LED is off.

// Some parameters for the calibration function. We have them global since we only use one at a time and dont need them to presist.
const int num_points = 40; //This is the number of points we take between start_voltage and end_voltage to find the turn-on voltage.
float measured_current[num_points]; //This is the current we measure at each voltage.
float measured_power[num_points]; //This is the optical power we measure at each voltage.

const float maxCurrentUsed = 0.95;  // We let the user set max current allowed for a LED, then this tells us what fraction of that we will actually use. I.e. if user sets 10A maximum then setting the LEd to full current means 9.5A...

// LED Timing parameters

int LEDTimeout[numLEDs] = {0}; // How long to next action in microseconds. Note leave this at 0 if it is not being used.
int LEDTriggerTime[numLEDs] = {0}; // When we last flipped the signal in microseconds.

int LEDDelayFactor = 5; // Approximate how long it takes the LED to turn on and settle at high powers in microseconds. This is used to make the timing more accurate for short exposures.

float LEDCurrentGain = 0.003*50; // The gain for the LED current sensing, 0.003 is a 3mOhm resisistor and 50 is gain of the op-amp.




void setLEDVoltageOutput(int channel, bool current = true){
    //Used to set whether the output of the LED voltage is going to be current or optical power. This is signa DB on LEDDrive schematic
    if (!LEDAttached) {//Not allowed to use if LED board isnt there.
        return;
    }
    float value = 0.0;
    if (!current){ // If we want to measure optical power
        value = 1.0; //We need to set input voltage to switch to high.
    }
    LEDOutputCurrent[channel-1] = current; //Records what mode we are in. Don't reset it elsewhere!
    setPWM(LED_PWM_ADR,channel+8,value);
    //Note the +8 is because the PWM channels for the LEDs start at location 9 in the PWM IC.

}

float readLEDOutput(int channel, bool current = true){
    if (!LEDAttached) { //Not allowed to use if LED board isnt there.
        return 0.0;
    }
    if (current != LEDOutputCurrent[channel-1]){ // If the system is currently not set to the right value we need to fix it.
        setLEDVoltageOutput(channel, current);
    }


    float value = readADC(channel,1); //Note the 1 refers to this being the ADC on the LED board.
    return value;
}


void setLEDFeedbackSignal(int channel, bool current = true){
    //Used to set whether the feedback signal controlling LED power is current (true) or optical (false) power.  This is signa DA on LEDDrive schematic
    if (!LEDAttached) {//Not allowed to use if LED board isnt there.
        return;
    }
    LEDFeedbackCurrent[channel-1] = current; // Records what mode we are in, DOnt change it elsehwere!

    float value = 0.0;
    if (!current){ // If we want to measure optical power
        value = 1.0; //We need to set input voltage to switch to high.
    }
    setPWM(LED_PWM_ADR,channel,value); //no +8 on channel since these are channels 1-8 on the PWM chip.
}


void setLEDLevelRaw(int channel, float voltage){
    // Deals with setting LED raw powers i.e. the analog voltage on the board.
    if (!LEDAttached) {//Not allowed to use if LED board isnt there.
        return;
    }
    if (voltage < 0.0){ //Prevent user setting power values that are out of range.
        voltage = 0.0;
    } else if (voltage > 3.2){
        voltage = 3.2; //This is the maximum voltage we can apply to the LED.
        raiseError("You tried to set the LED raw voltage above 3.2V. Technically 3.3V might be OK but we are reducing this so that there is some leeway for the various measurements not getting right to the 3V3 rail. If you are seeing this measurement it means you are trying to set it above the happily calibrated range. Potential fixes would be reduce current sense resistor or the optical sense resistor depending on which mdoe it is");
    }
    setLEDDACI2C(channel, voltage); //Now set the voltage to that LED. Note this doesn't turn it on, just sets up the intensity for when it is.
    
}

void setLEDLevel(int channel, float level = 0.0, bool current = true){
    //Used to set the power that would go to a given LED and what kind of feedback control is used. This is the CALIBRATED power which should be a number from 0 to 1.
    // Note it can be run on non-calibrated and non-attached LEDs, in which case its primary purpose is to make sure you are resetting the device to right feedback state etc.
    // Power is float from 0.0 (min power) to 1.0 (max power)
    // Current is a boolean. True means we want to set the LED to a current value, false means we want to set it to an optical power value.
    if (!LEDAttached) {//Not allowed to use if LED board isnt there.
        return;
    }

    

    if (current != LEDFeedbackCurrent[channel-1]){ // If the system is currently not set to the right value we need to fix it.
        setLEDFeedbackSignal(channel, current);
    }




    if (level < 0.0){ //Prevent user setting power values that are out of range.
        level = 0.0;
        raiseError("You tried to set the LED to a level less than 0.0. This is a bit weird, has something gone wrong?");
    } else if (level > 1.0){
        level = 1.0;
        raiseError("You tried to set the LED to a level greater than 1.0. This is a bit weird, has something gone wrong?");
    }

    
    float voltage = 0.0;
    bool notCalibrated = false;
    if (level == 0.0){
        voltage = 0.0; //If they are trying to set the LED to zero we set it to zero. This means we are basically allowing them to not calibrate it in this case.
    } else {
        if (current){ // If we are operating in current mode on that LED.
            //First check if the calibration voltages yet exist - i.e. they are non-zero.
            if (LEDTurnOnVoltageCurrent[channel-1] == 0.0){ //In this case you have not yet read the calibrated data from EEPROM since turning on the system or resetting it.
                LEDTurnOnVoltageCurrent[channel-1] = fread(channel-1+0*8); //Get it from EEPROM This is the turn-on voltage for the LED. It is the voltage at which the LED starts to emit light.
                LEDMaxVoltageCurrent[channel-1] = fread(channel-1+1*8); //Get it from EEPROM This is the maximum voltage that can be applied to the LED in current mode to achieve the maximum current.
                LEDZeroReadingCurrent[channel-1] = fread(channel-1+2*8); //Get it from EEPROM This is the read analog value when the LED is off.
                LEDTurnOnVoltagePower[channel-1] = fread(channel-1+3*8); // Note we dont actually need these power values here but we read them anyway to ensure they are in memory if the curren tones are.
                LEDMaxVoltagePower[channel-1] = fread(channel-1+4*8); 
                LEDZeroReadingPower[channel-1] = fread(channel-1+5*8); //Get it from EEPROM This is the read analog value when the LED is off.
                if (LEDTurnOnVoltageCurrent[channel-1] == 0.0||  LEDMaxVoltageCurrent[channel-1] == 0.0){ //In this case you have not yet read the calibrated data.
                    raiseError("You have not yet calibrated LED ID " + String(channel) + ". You need to run the calibrateLED function for that LED before you can set it to a current value and turn it on.");
                    notCalibrated = true;
                }
            }
            voltage = LEDTurnOnVoltageCurrent[channel-1] + level*(LEDMaxVoltageCurrent[channel-1]-LEDTurnOnVoltageCurrent[channel-1]); //This is the voltage we want to set the LED to.
        } else { // Now we do equiavlent stuff in optical power mode
            if (LEDTurnOnVoltagePower[channel-1] == 0.0){ //In this case you have not yet read the calibrated data from EEPROM since turning on the system or resetting it.
                LEDTurnOnVoltageCurrent[channel-1] = fread(channel-1+0*8);  // Note we dont actually need these current values here but we read them anyway to ensure they are in memory if the power ones are.
                LEDMaxVoltageCurrent[channel-1] = fread(channel-1+1*8); 
                LEDZeroReadingCurrent[channel-1] = fread(channel-1+2*8); 
                LEDTurnOnVoltagePower[channel-1] = fread(channel-1+3*8); //Get it from EEPROM This is voltage at which the LED starts to emit light in optical power mode, though as above currently it is calculated from measurements in current feedback mode.
                LEDMaxVoltagePower[channel-1] = fread(channel-1+4*8); //Get it from EEPROM This is the maximum voltage that can be applied to the LED in optical power mode to achieve the maximum current limit.
                LEDZeroReadingPower[channel-1] = fread(channel-1+5*8); //Get it from EEPROM This is the read analog value when the LED is off.
                if (LEDTurnOnVoltagePower[channel-1] == 0.0 ||  LEDMaxVoltagePower[channel-1] == 0.0){ //In this case you have not yet read the calibrated data.
                    raiseError("You have not yet calibrated LED ID " + String(channel) + ". You need to run the calibrateLED function for that LED before you can set it to an optical power value and turn it on. Or, you might be trying to put a LED with no optical sensor into optical control mode...");
                    notCalibrated = true;
                }
            }
            voltage = LEDTurnOnVoltagePower[channel-1] + level*(LEDMaxVoltagePower[channel-1]-LEDTurnOnVoltagePower[channel-1]); //This is the voltage we want to set the LED to.

        }
    }
    // if we decided they hadnt calibrated it properly we force it off.
    if (notCalibrated){
        voltage = 0.0; //If for whatever reason they are trying to turn the LED on but with level 0.0 we set it to "fully off" to ensure it doesnt turn on a little bit if our zero-point calibration is imperfect.
        raiseError("Setting LED ID " + String(channel) + " to zero power since it has not been calibrated properly.");
        level = 0.0; // Also set level to 0 since that is what is effectively being set to.
    } 
    setLEDLevelRaw(channel, voltage);
    LEDLevelSet[channel-1] = level; //This is a flag to say whether the LED level has been set since turning on the system. This is used to prevent the user from turning on the LED without setting the level first.

}

void switchLED(int channel, bool on = false, bool force = false){
    //This function is used to turn on or off a given LED
    // Note the force parameter is used when we are trying to enable the LED manually with a fixed voltage having not calibrated it - for example this happens in calibrateLED.

    if (!LEDAttached) {//Not allowed to use if LED board isnt there.
        return;
    }

    if (on == true){
        if (LEDLevelSet[channel-1]==0.0 && force == false && !on){ //If the level hasnt been set we dont allow the user to turn it on.
            raiseError("You have not yet set the level for LED ID " + String(channel) + ". You need to run the setLEDLevel function for that LED before you can turn it on.");
            on = false; // In this case we proceed to turn it off.
        }
    }

    int pinname;

    if (channel == 1){
        pinname = LED_1_Enable;
    } else if (channel == 2){
        pinname = LED_2_Enable;
    } else if (channel == 3){
        pinname = LED_3_Enable;
    } else if (channel == 4){
        pinname = LED_4_Enable;
    } else if (channel == 5){
        pinname = LED_5_Enable;
    } else if (channel == 6){
        pinname = LED_6_Enable;
    } else if (channel == 7){
        pinname = LED_7_Enable;
    } else if (channel == 8){
        pinname = LED_8_Enable;
    } else {
        raiseError("Invalid LED channel in switchLED");
        return;
    }    

    if (on){
        digitalWriteFast(pinname, HIGH);
    } else {
        digitalWriteFast(pinname, LOW);
    }

}

void switchLEDDirect(int channel, bool on = false, bool force = false){
    Serial.println("Called switchLEDDirect with args channel=" + String(channel) + " on=" + String(on) + " force" + String(force));
    // Basicaly a wrapper for switchLEd if we just want to turn on a LEd permanently. 
    if (on && !force){ //If the level hasnt been set we dont allow the user to turn it on.
        if (LEDLevelSet[channel-1]>=0.3){
            raiseError("You are not allowed to set a LED permanently on if the level is more than 30% max power due to heating issues in case your code crashes and leaves it on. You need to run the switchLEDTimed function for that LED instead. ");
            on = false; // In this case we proceed to turn it off to avoid any weird mishaps
        } else if (LEDLevelSet[channel-1]==0.0){
            raiseError("You are trying to turn a LED on that has level set to zero which is a bit odd, but allowed. ");
        }
    }
    switchLED(channel, on);
    if (on == false){
        resetLEDTimeout(channel);
    }
}

void resetLEDTimeout(int channel) {
    LEDTimeout[channel-1] = 0; //Reset the timeout
    NumberLEDsBeingTimed = NumberLEDsBeingTimed -1; //Decrement the number of LEDs being timed.
}

void switchLEDTimed(int channel, float time = 0.0, bool on = false){
    // Function to turn on the LED when we want it to be on for a certain amount of time - note it always ends!
    // Incoming time is in ms
    if (!LEDAttached) {//Not allowed to use if LED board isnt there.
        return;
    }
    if (time<=0.0){
        raiseError("You tried to turn on LED ID " + String(channel) + " for 0.0ms. This is a bit weird, has something gone wrong?");
        return;
    } else if (time > 3600.0*1000.0){
        raiseError("You tried to turn on LED ID " + String(channel) + " for a more than an hour, has something gone wrong?");
        return;
    }

    if (on && LEDLevelSet[channel-1]==0.0){ //If the level hasnt been set we dont allow the user to turn it on.
        raiseError("You are trying to turn a LED on that has level set to zero which is a bit odd, but allowed.");
    }

    if (on == false){ // If they have asked it to switch off we do all the good hygene of turning it off
        switchLED(channel, false); //Turn LED off
        resetLEDTimeout(channel);
        return;

    } else if (on == true && LEDTimeout[channel-1] > 0){ // If they have asked it to switch on but it is already on we do nothing. 
        raiseError ("You tried to turn on LED ID " + String(channel) + " with a timer but it is already on. We need to stop this from happening since it would reset the timeout and we wouldnt know when to turn it off due to NumberLEDsBeingTImed going haywire.");
        return;
    } else if (on == true && LEDLevelSet[channel-1]==0.0){ //If the level hasnt been set but they want it on
        raiseError("You have not yet set the level for LED ID " + String(channel) + " but you are trying to turn it on, potentially this is an error.");
     } else if (on == true && (LEDLevelSet[channel-1]>0.3 && LEDTimeout[channel-1] > 10000.0)){ // If they have asked an awful lot of power i.e. more than 30% for more than 10 seconds.
        raiseError("You are trying to turn on LED ID " + String(channel) + " for more than 10 seconds at more than 30 precent of maximum power. It might generate a fair amount of heat. If you want to enable this you need to adjust the code :) ");
        return;     
    }

    
    //If we get to this point we are basically going to let them do what they wanted!!
    
    LEDTimeout[channel-1] = time*1000+LEDDelayFactor; //Convert time to microseconds and set the timeout, note here we are rounding to integer microseconds.
    //Note LEDDelayFactor is a fairly approximate estimate of how long do we thing it takes the system to turn on at relatively high powers (e.g. 0.5 or so) so that when we are dealing with fast exposures we are more accurate.
    NumberLEDsBeingTimed = NumberLEDsBeingTimed +1; //Increment the number of LEDs being timed.

    switchLED(channel, true); //Turn LED on
    LEDTriggerTime[channel-1] = micros(); //Set the trigger time to now.
    

    if (LEDTimeout[channel-1]<100) { // If we are trying to do a very short flash of the LED less than 100us then we will time it here so it can be fairly precise.
        delayMicroseconds(LEDTimeout[channel-1]); //
        switchLED(channel, false); //Turn LED off
        resetLEDTimeout(channel);
        if (LEDTimeout[channel-1] < 10){
            raiseError("You tried to turn a LED on for less than 10us. That is very fast and going to be rather inaccurate...!");
        }
    }
    //We end here. If the LED is still on it would get turned off inside the LEDTimingHandler function which is called from the main.cpp loop.

}

void turnLEDsOff(){
    //This function is used to turn off all LEDs. Can happen even if we havent got the LEDs attached.
    digitalWriteFast(LED_1_Enable, LOW);
    digitalWriteFast(LED_2_Enable, LOW);
    digitalWriteFast(LED_3_Enable, LOW);
    digitalWriteFast(LED_4_Enable, LOW);
    digitalWriteFast(LED_5_Enable, LOW);
    digitalWriteFast(LED_6_Enable, LOW);
    digitalWriteFast(LED_7_Enable, LOW);
    digitalWriteFast(LED_8_Enable, LOW);
}





void LEDTimingHandler(){
    //This function checks all the LEDs and timers etc, and turns them off if they have been on for too long.
    //It gets called from the main.cpp loop so long as NumberLEDsBeingTimed  is >0, and it updates the global number of LEDs being timed - maybe this is a naughty way to handle things as globals but yolo.

    if (!LEDAttached) {//Not allowed to use if LED board isnt there.
        raiseError("You tried to use the LEDTimingHandler but the LED board is not attached - this shouldn't generally be possible not sure how you managed it.");
        return;
    }

    for (int i = 0; i < numLEDs; i++){ //Loop through all the LEDs
        if (LEDTimeout[i] > 0){ // This should be non-zero if the LED is on and being timed.
            if (micros() - LEDTriggerTime[i] > LEDTimeout[i]){
                switchLED(i+1, false); //Turn LED off
                resetLEDTimeout(i+1);
//                LEDTimeout[i] = 0; //Reset the timeout
//                NumberLEDsBeingTimed = NumberLEDsBeingTimed -1; //Decrement the number of LEDs being timed.
            }
        }
    }

    if (NumberLEDsBeingTimed<0){
        raiseError("You have somehow got a negative number of LEDs being timed. This is a bit weird, has something gone wrong?");
        NumberLEDsBeingTimed = 0;
    }

}

float measurePhotodiode(int channel){
    
    if (!LEDAttached) {//Not allowed to use if LED board isnt there.
        return;
    }
    setLEDVoltageOutput(channel, false); // Tell it we want to read back optical power from that LED.   
    int num_readings_to_average = 1;
    float sum = 0.0;
    for (int i = 0; i < num_readings_to_average; i++){
        sum += readADC(channel,1);
    }
    float readVoltage = sum/(num_readings_to_average*1.0);
    float result = (readVoltage-0.3)*1000.0 ;  // 1000 converts to mV. We are using a generic 0.3 offset since htis is approximately what we expect for any given channel.
    return result;
}

void measureLED(int channel, float* result){
    //Note result is a 2 element array coming in within which we can store our values.
    //This function is used to measure the current current and opticla power from a LED. It converts current back to A but optical power is left in mV absence some effective conversion means.
    // Set it to read the current value.
    result[0]=-1.0; // SOme dummy values that will get returned if things dont work in here.
    result[1]=-1.0;
    if (!LEDAttached) {//Not allowed to use if LED board isnt there.
        return;
    }
    if (LEDTurnOnVoltageCurrent[channel-1] == 0.0 ||  LEDMaxVoltageCurrent[channel-1] == 0){
        raiseError("You have either not set the level or not yet calibrated LED ID " + String(channel) + ". You need to set a level on the LED before you turn it on (let alone measure current through it)");
        return;
    
    }
    
    setLEDVoltageOutput(channel, true); // Tell it we want to read back current from that LED.
    int num_readings_to_average = 50;
    float sum = 0.0;
    for (int i = 0; i < num_readings_to_average; i++){
        sum += readADC(channel,1);
    }
    float readVoltage = sum/(num_readings_to_average*1.0);
    float currentMeasured =((readVoltage-LEDZeroReadingCurrent[channel-1]) / (LEDCurrentGain)); 
    result[0] = currentMeasured; //This is the current in A.
    


    // Below is a test mode which is designed to see how much noise there is in our current measurement for testing purposes, comment out if not needed.
    setLEDVoltageOutput(channel, false); // Tell it we want to read back optical power

    float value;
    float pastvalue;
    float meandiff = 0.0;
    for (int i = 0; i < num_readings_to_average; i++){
        value = readADC(channel,1);
        if (i>0){
            meandiff += abs(value-pastvalue);
            pastvalue = value;
        }
    }
    meandiff = 1000.0*(meandiff/(num_readings_to_average-1)); // Convert to milivonts and average over the number of readings.
    Serial.println("Mean diff is " + String(meandiff,6 ) + "mV for LED " + String(channel) + " at current " + String(currentMeasured,6) + "A");




    setLEDVoltageOutput(channel, false); // Tell it we want to read back optical power  from that LED.
    sum = 0.0;
    for (int i = 0; i < num_readings_to_average; i++){
        sum += readADC(channel,1);
    }
    readVoltage = sum/(num_readings_to_average*1.0);
    float powerMeasured =(readVoltage-LEDZeroReadingPower[channel-1])*1000.0 ;  // 1000 converts to mV.
    result[1] = powerMeasured; //This is the optical sensor voltage in mV

    // Note there is an error in this function, somehow it should depend on what signal is being used for feedback? Or should it? 
}

void getLEDSetup(int channel, float* result){
    result[0] = LEDLevelSet[channel-1];
    if (result[0]!=0.0){ // If the above has been set this should not be zero.
        result[1] = (LEDMaxVoltageCurrent[channel-1] -  LEDTurnOnVoltageCurrent[channel-1])*LEDLevelSet[channel-1] / LEDCurrentGain; //This is the current level the LED would achieve at current setting
        result[2] = (LEDMaxVoltagePower[channel-1] -  LEDTurnOnVoltagePower[channel-1])*LEDLevelSet[channel-1]*1000.0; //This is the optical output it would get
        result[3] = (LEDMaxVoltageCurrent[channel-1] -  LEDTurnOnVoltageCurrent[channel-1]) / LEDCurrentGain; //This is the maximum current we are allowing it to deliver.
    } else { // In this case most likely they havent calibrated it or set level yet.
        result[1] = 0.0;
        result[2] = 0.0;
        result[3] = 0.0;
        raiseError("You have not yet set up the level for LED ID " + String(channel) + " properly. You need to run the setLEDLevel function for that LED before you can get meaningful parameters for its setup.");    
    }
    
}


void resetLEDs(bool hardwareActive = true){ // Had to put this near bottom since it calls various functions above.
    // This function can be called to reset the config of the LED driver back to default
    // Note it sets everthing back to current mode. 
    // hardwareActive is a flag that tells us whether or not we think the LED hardware is able to be communicated with - in particular for example if our power supplies and heartbeat etc on the LED board is enabled at that time.
    if (!LEDAttached) {//Not allowed to use if LED board isnt there.
        return;
    }
    turnLEDsOff(); //Turn off all LEDs - shouldnt be necessary but here just in case.
    for (int i = 0; i < numLEDs; i++){
        if (hardwareActive){
            setLEDFeedbackSignal(i+1, true); //Set the feedback signal to be current. Note the +1 is because the LED channels are 1-8 but the array is 0-7.
            setLEDVoltageOutput(i+1, true); //Set the output voltage to be current. Note the +1 is because the LED channels are 1-8 but the array is 0-7.
            setLEDLevel(i+1, 0.0, true); //Set the LED to be off. Note the +1 is because the LED channels are 1-8 but the array is 0-7.
        }
        LEDLevelSet[i] = 0.0; //This is a flag to say whether the LED level has been set since turning on the system. This is used to prevent the user from turning on the LED without setting the level first.
        LEDTimeout[i] = 0; // How long to next action in microseconds. Note leave this at 0 if it is not being used.
        LEDTriggerTime[i] = 0; // When we last flipped the signal in microseconds.
    }
    
}





void calibrateLED(int channel, float maxCurrent = 5.0){
    // This function is used to calibrate a given LED. It is used to find the turn-on voltage, relationship between voltage and current, and relationship between voltage and optical power.
    // channel is which chnnel we are calibrating. This is a number from 1 to 8.
    // maxCurrent is whatever we want to set the maximum current limit as (in amps)
    if (!LEDAttached) {//Not allowed to use if LED board isnt there.
        return;
    }
    // This should be run whenever a new LED is connected to a new channel. It takes some time to run.
    //Each LED has some voltage range over which it can run which starts from turnOnVoltage and goes up to maxVoltage (which is where current exceeds some maximum). This maps to [0,1] in terms of the "level" of that LED. 
    // Note we COULD make this more complex by figuring out if the turn-on points change when we are in optical versus current feedback mode but we assume they are the same for now. 
    
    // // Read current config of what these parameters are just for lols. -1 is because channels index from 1 and memory from 0.
    // float turnOnVoltageCurrent_old = fread(channel-1+0*8); //This is the turn-on voltage for the LED. It is the voltage at which the LED starts to emit light.
    // float maxVoltageCurrent_old = fread(channel-1+1*8); //This is the maximum voltage that can be applied to the LED in current mode to achieve the maximum current.
    // float turnOnVoltagePower_old = fread(channel-1+2*8); //This is voltage at which the LED starts to emit light in optical power mode, though as above currently it is calculated from measurements in current feedback mode.
    // float maxVoltagePower_old = fread(channel-1+3*8); //This is the maximum voltage that can be applied to the LED in optical power mode to achieve the maximum current limit.

    //Note that we EXPECT the turn on to be very close to 0.3 so the below should flank this value.
    float start_voltage = 0.25; //This is the voltage we start at when we are trying to find the turn-on voltage.
    float end_voltage = 0.35; //This is the voltage we stop at when we are trying to find the turn-on voltage.

    int num_readings_to_average = 20;

    setLEDFeedbackSignal(channel, true); // We are going to be using current feedack on this LED for the hardware feedback.

    for (int i = 0; i < num_points; i++){
        float voltage = start_voltage + (end_voltage-start_voltage)*i/(num_points-1); //Calculate voltage we want to set
        // Serial.println("Setting voltage to " + String(voltage) + " for LED " + String(channel) + " in calibrateLED");
        setLEDLevelRaw(channel, voltage); // Set the voltage as needed to the ADC on this board.
        switchLED(channel, true, true); //Turn LED on. Forceflag is true.
        
        //Now we do a Heartbeat trigger since this long calibration functionc an oherwise crash the heartbeat. Adds also a delay time for the LEd to settle.
        digitalWriteFast(Heartbeat, LOW);
        delay(3);
        digitalWriteFast(Heartbeat, HIGH);
        delay(3);
        digitalWriteFast(Heartbeat, LOW);
        
        // Now take several current readings and average them out. The reason current/power arent interleaved is that it takes a long time to switch between current and power mode as it requires I2C commands to PWM chip.
        float current_sum = 0.0;
        for (int j = 0; j < num_readings_to_average; j++){
            current_sum += readLEDOutput(channel, true);
        }
        measured_current[i] = current_sum/num_readings_to_average;
        // Now take several power readings and average them out. 
        float power_sum = 0.0;
          for (int j = 0; j < num_readings_to_average; j++){
            power_sum += readLEDOutput(channel, false);
        }
        measured_power[i] = power_sum/num_readings_to_average;
                
        switchLED(channel, false, true); //Turn LED  OFF

    }


    // Now we have the current and power values at each voltage. We can use this to find the turn-on voltage and the relationship between voltage and current and optical power.
    //First get some statistics for the max/min and average value for each in the first 10 readings which we assume are all "off"
    float zeroReadingCurrent = 0.0;
    float czv_min = measured_current[0];
    float czv_max = measured_current[0];
    float zeroReadingPower = 0.0;
    float ozv_min = measured_current[0];
    float ozv_max = measured_current[0];
    int num_pts_baseline = num_points/4; //We assume the first quarter of the points are all "off" and we use this to get the zero voltage and the min/max values.
    
    for (int i = 0; i < num_pts_baseline; i++){
        zeroReadingCurrent += measured_current[i];
        if (measured_current[i] < czv_min){
            czv_min = measured_current[i];
        }
        if (measured_current[i] > czv_max){
            czv_max = measured_current[i];
        }
        zeroReadingPower += measured_power[i];
        if (measured_power[i] < ozv_min){
            ozv_min = measured_power[i];
        }
        if (measured_power[i] > ozv_max){
            ozv_max = measured_power[i];
        }
    }
    zeroReadingCurrent = zeroReadingCurrent/num_pts_baseline; //divide down to get average
    zeroReadingPower = zeroReadingPower/num_pts_baseline; // divide down to get average
    float czv_threshold = (czv_max-czv_min) + zeroReadingCurrent; //This is the threshold for when we consider the LED to be "on" in terms of current.
    float ozv_threshold = (ozv_max-ozv_min) + zeroReadingPower; //This is the threshold for when we consider the LED to be "on" in terms of optical power.

    //Now we iterate through the recorded values and record the location of the first time the system is both bigger than the threshold and bigger than 0.3V.
    int turn_on_index = -1;
    for (int i = 0; i < num_points; i++){
        if (measured_current[i] > czv_threshold && start_voltage + (end_voltage-start_voltage)*i/(num_points-1) > 0.3){
            turn_on_index = i;
            break;
        }
    }

    //Now we know when it is actually on in a measurable way. But we want to precisely determine the zero-crossing of this LED. So we do a linear fit manually since we hate C++ toolboxes.
    // Initialize sums
    float sum_x = 0, sum_y = 0, sum_xx = 0, sum_xy = 0;
    int num_points_in_fit= num_points - turn_on_index;

    for (int i = turn_on_index; i < num_points; i++){
        float voltage = start_voltage + (end_voltage-start_voltage)*i/(num_points-1); //Calculate voltage we want to set
        // Update sums
        sum_x += voltage;
        sum_y += measured_current[i];
        sum_xx += voltage * voltage;
        sum_xy += voltage * measured_current[i];
    }

    // Calculate slope and intercept
    float slope = (num_points_in_fit * sum_xy - sum_x * sum_y) / (num_points_in_fit * sum_xx - sum_x * sum_x);
    float intercept = (sum_y - slope * sum_x) / num_points_in_fit;
    //Note for this we have used CURRENT measurements rather than optical measurements but they should in theory be the same. We go with current since we think it might be lower noise.

    //Then the question is where does this intersect the "certainly off" state of the first 10 values which is zeroReadingCurrent.
    float turnOnVoltageCurrent = (zeroReadingCurrent - intercept)/slope;
    // Serial.println("Current turn on voltage is " + String(turnOnVoltageCurrent,6)+ " and current zero voltage was " + String(zeroReadingCurrent,6)); // Print whatever it is to 6SF.



    //Now we do the same thing but for optical power.
    sum_x = 0, sum_y = 0, sum_xx = 0, sum_xy = 0;
    num_points_in_fit= num_points - turn_on_index;
    for (int i = turn_on_index; i < num_points; i++){
        float voltage = start_voltage + (end_voltage-start_voltage)*i/(num_points-1); //Calculate voltage we want to set
        // Update sums
        sum_x += voltage;
        sum_y += measured_power[i];
        sum_xx += voltage * voltage;
        sum_xy += voltage * measured_power[i];
    }

    // Calculate slope and intercept
    slope = (num_points_in_fit * sum_xy - sum_x * sum_y) / (num_points_in_fit * sum_xx - sum_x * sum_x);
    intercept = (sum_y - slope * sum_x) / num_points_in_fit;
    float turnOnVoltagePower = (zeroReadingPower - intercept)/slope;
    // Serial.println("Optical turn on voltage is " + String(turnOnVoltagePower,6) + " and optical zero voltage was " + String(zeroReadingPower,6)); // Print whatever it is to 6SF.
    // Serial.println("Slope is " + String(slope,6) + " and intercept is " + String(intercept,6)); // Print whatever it is to 6SF.

    //Now we are going to work out relationship between current and optical power over the whole range of values up to the maximum current.


    start_voltage = turnOnVoltageCurrent + 0.03; //This is where we will now start ramping - the 0.03 should put it well and truly "on"
    end_voltage = turnOnVoltageCurrent + maxCurrent*maxCurrentUsed*LEDCurrentGain; 

    
    //Now we set all values in measured_current and measured_power to zero.
    for (int i = 0; i < num_points; i++){
        measured_current[i] = 0.0;
        measured_power[i] = 0.0;
    }
    
    num_readings_to_average = 10;

    float measured_power_max = 0.0;
    for (int i = 0; i < num_points; i++){
        float voltage = start_voltage + (end_voltage-start_voltage)*i/(num_points-1); //Calculate voltage we want to set
        setLEDLevelRaw(channel, voltage); // Set the voltage as needed to the ADC on this board.
        switchLED(channel, true, true); //Turn LED
       //Now we do a Heartbeat trigger since this long calibration functionc an oherwise crash the heartbeat. Adds also a delay time for the LEd to settle.
        digitalWriteFast(Heartbeat, LOW);
        delay(1);
        digitalWriteFast(Heartbeat, HIGH);
        delay(1);
        digitalWriteFast(Heartbeat, LOW);
        
        // Now take several current readings and average them out. The reason current/power arent interleaved is that it takes a long time to switch between current and power mode as it requires I2C commands to PWM chip.
        float current_sum = 0.0;
        for (int j = 0; j < num_readings_to_average; j++){
            current_sum += readLEDOutput(channel, true);
        }
        measured_current[i] = current_sum/num_readings_to_average;
        // Now take several power readings and average them out. 
        float power_sum = 0.0;
          for (int j = 0; j < num_readings_to_average; j++){
            power_sum += readLEDOutput(channel, false);
        }
        measured_power[i] = power_sum/num_readings_to_average;
        if (measured_power[i] > measured_power_max){
            measured_power_max = measured_power[i];
        }
        switchLED(channel, false, true); //Turn LED  OFF

    }
    
    Serial.println("Measured power max is " + String(measured_power_max,6));
    // Now we have the current and power values at each voltage. We can use this to find the turn-on voltage and the relationship between voltage and current and optical power.
    // What we want to do now is find the voltage at which the current values cross 10A and set this as our maximum. Then, determine what that corresponds to in terms of optical power.

    //First we need to go through and figure out if at some point the curent limiter was triggered on the LED. If this happens what will happen is the feedback circuit gets cut off so the current measurement ADC reading should be ¬0.3
    int current_limit_index = -1;
    for (int i = 2; i < num_points; i++){ //Note we skip first few points in case noise drops them below the limit.
        if (measured_current[i] <start_voltage){ //Note we are in current feedback mode so anything that is lower than our start voltage indicates it must have triggered the auto-off circuit.
            current_limit_index = i;
            break;
        }
    }
    
    if (current_limit_index >=0 && current_limit_index<num_points-2){ //Note we allow it to over-current in the last three points since it might be a bit noisy and technically we are setting it very close to upper bound here...
        float voltageUnhappy = start_voltage + (end_voltage-start_voltage)*current_limit_index/(num_points-1);
        float currentValueUnhappy = (voltageUnhappy-turnOnVoltageCurrent)/(LEDCurrentGain); //This is the current that the LED was approximately being set to when it became unhappy
        raiseError("You tried to set LED ID " + String(channel) + " to have a maximum current of " + String(maxCurrent,6) + "A current in calibration. However, when the calibration process attempted to set it to a current of " + String(currentValueUnhappy,6) + "A the LED was not happy with this - it seems to have triggered the over-current protection circuit in hardware. This could mean that you need to adjust the current limiter resistors on the PCB for that channel if you wish to go higher than this...");
        
    }
    
    int num_points_allowed;
    float maxVoltageCurrent = 0.0;
    float maxVoltagePower = 0.0;
    if (current_limit_index >=0){
        num_points_allowed = current_limit_index-1; //We dont want to include the point where it went over the limit.
        // In this we use the last allowable point as the max allowed voltage
        maxVoltageCurrent = measured_current[num_points_allowed];
        maxVoltagePower = measured_power[num_points_allowed];
        raiseError("Note your calibration of LED ID " + String(channel) + " was cut short by the hardware over-current protection circuit. This means that the maximum current and optical power values are not as accurate as they could be. You should reduce the maxCurrent allowed for that LED so it isnt tripping that channel's power level circuit. Or, if you want it to go to higher power, need to adjust the circuit current limiting resistors");
    } else { // Otherwise we just take the biggest value we got.
        num_points_allowed = num_points-1;
        maxVoltageCurrent = measured_current[num_points_allowed];
        maxVoltagePower = measured_power[num_points_allowed];
        //Note here we could do another linear fit to try to get the exact point it reaches maxCurrent*maxCurrentUsed but we dont bother since we are already close enough.
        // Serial.println("Max current is " + String(maxVoltageCurrent,6) + " and max power is " + String(maxVoltagePower,6));
    }

    if (measured_power_max<0.32){ // 0.32 arbitrary value that should generally be exceeded by working systen. In this case it seems like the optical sensor might not be attached so we need to prevent them from using optical feedback etc.
        raiseError("You have calibrated LED ID " + String(channel) + " and it seems like the optical sensor is not attached or not working. This means you should not use optical feedback for this LED. You should set the level of this LED using current feedback only.");
        turnOnVoltagePower = 0.0;
        maxVoltagePower = 0.0;
        zeroReadingPower = 0.0;
    }
    // float maxVoltagePower_old = fread(channel-1+4*8); //This is the maximum voltage that can be applied to the LED in optical power mode to achieve the maximum current limit.
    // Serial.println("Max power old is " + String(maxVoltagePower_old,6) + " and new value is " + String(maxVoltagePower,6));

    //At this point we know the voltages needed for the LED to reach the maximum current and optical power. We can now write these to memory.
    fwrite(channel-1+0*8, turnOnVoltageCurrent); //This is the turn-on voltage for the LED. It is the voltage at which the LED starts to emit light.
    fwrite(channel-1+1*8, maxVoltageCurrent); //This is the maximum voltage that can be applied to the LED in current mode to achieve the maximum current.
    fwrite(channel-1+2*8, zeroReadingCurrent); //This is voltage at which the LED starts to emit light in optical power mode, though as above currently it is calculated from measurements in current feedback mode.
    fwrite(channel-1+3*8, turnOnVoltagePower); //This is voltage at which the LED starts to emit light in optical power mode, though as above currently it is calculated from measurements in current feedback mode.
    fwrite(channel-1+4*8, maxVoltagePower); //This is the maximum voltage that can be applied to the LED in optical power mode to achieve the maximum current limit.#
    fwrite(channel-1+5*8, zeroReadingPower); //This is voltage at which the LED starts to emit light in optical power mode, though as above currently it is calculated from measurements in current feedback mode.

    //We now put that into our local memory. The reason for this is the setLevel function would otherwise not know these values have changed mid-experiment.
    LEDTurnOnVoltageCurrent[channel-1] = turnOnVoltageCurrent;  // Note we dont actually need these current values here but we read them anyway to ensure they are in memory if the power ones are.
    LEDMaxVoltageCurrent[channel-1] =  maxVoltageCurrent; 
    LEDZeroReadingCurrent[channel-1] = zeroReadingCurrent; 
    LEDTurnOnVoltagePower[channel-1] = turnOnVoltagePower; //Get it from EEPROM This is voltage at which the LED starts to emit light in optical power mode, though as above currently it is calculated from measurements in current feedback mode.
    LEDMaxVoltagePower[channel-1] = maxVoltagePower; //Get it from EEPROM This is the maximum voltage that can be applied to the LED in optical power mode to achieve the maximum current limit.
    LEDZeroReadingPower[channel-1] = zeroReadingPower; //Get it from EEPROM This is the read analog value when the LED is off.




    // // Now for debugging I want to print out the curren and power values as one big string
    // String current_string = "Current values: ";
    // String power_string = "Power values: ";
    // String voltage_string = "Voltage values: ";
    // for (int i = 0; i < num_points; i++){
    //     current_string += String(measured_current[i],6) + ",";
    //     power_string += String(measured_power[i],6) + ",";
    //     voltage_string += String(start_voltage + (end_voltage-start_voltage)*i/(num_points-1),6) + ",";
    // }
    
    // Serial.println(voltage_string);
    // Serial.println(current_string);
    // Serial.println(power_string);

    return;



}
