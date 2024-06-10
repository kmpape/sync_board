#include <Arduino.h>
#include "PinOut.h"
#include "GlobalVariables.h"
#include "SerialController.h"
#include <SPI.h>
#include <Wire.h>
#include "digitalWriteFast.h"
#include "ICDrivers.h"
#include "LEDDriver.h"

bool gpiosEnabled = false; // This is a flag to say if the GPIOs are enabled or not. It is set to false when the system is disabled and true when it is enabled. It is used to prevent the user from reconfiguring the GPIOs while the system is running.

float readADC(int channel, int ADC_ID = 0){
    //Wrapper for readADC, pretty simple since most of it happens in ICDrivers.
    //Note that to do this read it needs to first write to the device a control byte - meaning transmission of the address + control byte with acks which is approx 10*2=20 bits.
    //Then it needs to send an address and read two bytes of data so nother 30 bits. Together with the 20 bits above that is 50 bits. Running at 100khz clock means 500us per read MINIMUM and in practice it is worse than this as there are various other delays.
    
    // Note ADC ID 0 is the one on the SyncBoard and ADC ID 1 is the one on the LEDDriver.

    //First check channel is between 1 and 8
    bool internal = false; // This is a flag to say if we are reading ADC using internal (true) or external (false) references.
    if (channel < 1 || channel > 8){
        raiseError("Sorry you tried to read from an ADC channel that doesnt exist. You tried to read from channel "+String(channel)+". Better luck next time.");
        return -1.0;
    }
    //Now convert to DAC channels by subtracting 1
    channel = channel - 1;
    //Now read the ADC
    uint16_t ADCValue = readADCOnce(channel, internal, ADC_ID);
    //Now convert this binary value to a float considering that the reference voltage is 3.3V and the ADC is 12 bits.
    float conversion_voltage;
    if (internal){
        conversion_voltage = 2.5; //Internal reference is 2.5V
    } else {
        conversion_voltage = 3.3; // This is Ref1 on the PCB a precise 3V3 reference.
    }
    float ADCOut = ADCValue*conversion_voltage/4096;
    
    return ADCOut;

}

void setDAC(int channel, float value){
    // Wrapper around setDACSPI to make it easier to use.
    // First check the channel is in range
    if (channel < 0 || channel > 8){
        raiseError("You tried to set DAC channel "+String(channel)+" but it is out of range. Must be between 0 and 8.");
        return;
    }
    // Now check the voltage is in range
    if (value < 0.0 || value > 3.3){
        raiseError("You tried to set DAC channel "+String(channel)+" to "+String(value)+"V but it is out of range. Must be between 0 and 3.3V. Setting to 0V");
        value = 0.0;
    }
    
    setDACSPI(channel, value);
    
}

int GPIOPinMap(int pin){ // This function maps the pin number to the PWM chip and channel number. It is based on the Teensy pin out of the GPIO.
    // Note this is a bit of a hack and should be replaced with a proper lookup table.
    //Returns -1 for GPIO29/30 which are analog inputs and dont have level shifters.
    if (pin == GPIO_13){
        return 1;
    } else if (pin == GPIO_25){
        return 3;
    } else if (pin == GPIO_26){
        return 5;
    } else if (pin == GPIO_27){
        return 7;
    } else if (pin == GPIO_28){
        return 9;
    } else if (pin == GPIO_29){
        return -1; // Doesnt have a level shifter, also is analog input.
    } else if (pin == GPIO_30){
        return -1; // Doesnt have a level shifter, also is an analog input.
    } else if (pin == GPIO_31){
        return 11;
    } else if (pin == GPIO_32){
        return 13;
    } else {
        raiseError("GPIO pin not recognised");
        return -2;
    }
}

int GPIOPinMap2(int pin){ // This maps the pin number given by user. Note it has no relevance to the Teensy pin, it is trying to make user's life easier.
    //Note that this thing relies on the convenient fact that none of the GPIOs have a name <=8
    if (pin == 13){
        return 0;
    } else if (pin == 25){
        return 1;
    } else if (pin == 26){
        return 2;
    } else if (pin == 27){
        return 3;
    } else if (pin == 28){
        return 4;
    } else if (pin == 29){
        return 5; 
    } else if (pin == 30){
        return 6; 
    } else if (pin == 31){
        return 7;
    } else if (pin == 32){
        return 8;
    } else if (pin>=0 && pin<=8){ // In this case the user sent us something indexced from 0-8 so we can just return it without change.
        return pin;
    } else {
        raiseError("GPIO pin not recognised");
        return -1;
    }
}

int GPIOPinMap3(int pin){ //Now do the opposite of the above
    if (pin == 0){
        return GPIO_13;
    } else if (pin == 1){
        return GPIO_25;
    } else if (pin == 2){
        return GPIO_26;
    } else if (pin == 3){
        return GPIO_27;
    } else if (pin == 4){
        return GPIO_28;
    } else if (pin == 5){
        return GPIO_29; 
    } else if (pin == 6){
        return GPIO_30; 
    } else if (pin == 7){
        return GPIO_31;
    } else if (pin == 8){
        return GPIO_32;
    } else {
        raiseError("GPIO pin not recognised");
        return -1;
    }
}


void setupGPIO (int pin, bool enabled = false, int GPIOFunction = 0,bool GPIOInput = false){
    // Note if you have jumped these signals to either the expansion boards or around the level shifters these commands have no meaningful effect.
    // pin is the pin number of the GPIO
    // GPIOInput is a bool, true means input, false means output. Note this is ignored if you are doing PWM or Analog modes.
    // GPIOFunction is an int, 0 means digital GPIO, 1 means PWM, 2 means Analog Input (not implemented yet)
    // enabled is a bool, false means disable that GPIO entirely. 
    
        
        
    int GPIODirPin=GPIOPinMap(pin);
    int GPIOEnPin=GPIOPinMap(pin)+1;
    
    if (!enabled) { //If we want to disable the level shifter.
        pinModeFast(pin, INPUT); // Set it as an input for high impedance
        if (GPIOEnPin>0){ //This wnt happen if we are doing GPIO29/30 as they dont have level shifters.
            setPWM(SyncBoard_PWM_LVLShift_ADR, GPIOEnPin, 0); // Set the enable pin to low to disable the level shifter.
            setPWM(SyncBoard_PWM_LVLShift_ADR, GPIODirPin, 0); // Set the direction pin to low so it is set up as an input i.e. high imedpance.
        }
        
        return; // We are done so return.
    }


    if (GPIOFunction==0){ //Default state which is digital I/O with voltage defined by jumpers. 
        if (GPIOInput){ //Input
            pinModeFast(pin, INPUT);
            if (GPIOEnPin>0){ //This wnt happen if we are doing GPIO29/30 as they dont have level shifters.
                setPWM(SyncBoard_PWM_LVLShift_ADR, GPIODirPin, 0); // Set the direction pin to low to set it as input direction
                setPWM(SyncBoard_PWM_LVLShift_ADR, GPIOEnPin, 1); // Set the enable pin to high to enable the level shifter. 
            }
           

        } else { //Output
            pinModeFast(pin, OUTPUT);
            digitalWriteFast(pin, LOW); //default setting for all GPIOs Output is low.
            if (GPIOEnPin>0){ //This wnt happen if we are doing GPIO29/30 as they dont have level shifters.
                setPWM(SyncBoard_PWM_LVLShift_ADR, GPIODirPin, 1); // Set the direction pin to high to set it as output direction
                setPWM(SyncBoard_PWM_LVLShift_ADR, GPIOEnPin, 1); // Set the enable pin to high to enable the level shifter. 
            }
        }
        // todo: Something with the PWM chip to set up the direction and turn it on.

    } else if (GPIOFunction==1){ //PWM
        // Add something here if ever used.
        if (GPIOInput){
            raiseError("You are trying to set up a PWM GPIO as an input which doesnt make sense.");
        } else {
            raiseError("GPIO function PWM is not yet implemented. ");
        }
        
    } else if (GPIOFunction==2){ //Analog Input
        // pinMode(pin, INPUT); //todo fix this if we ever need it. Currently we don't as we have the other ADC but we could usse these if favourable.
        raiseError("Setting GPIO as analog input is not yet implemented. ");
       if (!GPIOInput){
            raiseError("You are trying to set up a Analog GPIO as an output which doesnt make sense.");
        } else {
            raiseError("GPIO function analog is not yet implemented");
        }
        
    } else {
        raiseError("GPIO function not recognised");
    }
    

}

void enableGPIOandLevelShift(){
    if (gpiosEnabled){
        return; // DO nothing if they are already enabled.
    } else {
            setPWM(SyncBoard_PWM_LVLShift_ADR,15,0); //Set 15 which is En7 on schematic to low to enable the bulk level shifters
            setPWM(SyncBoard_PWM_LVLShift_ADR,16,1); //Set 16 which is En8 on schematic to high to enable the 3V3 source to bulk level shifters (i.e. power net 3V3S)
    }
    gpiosEnabled = true;
}

void disableGPIOandLevelShift(){
    if (!gpiosEnabled){
        return; // DO nothing if they are already disabled.
    } else {
            setPWM(SyncBoard_PWM_LVLShift_ADR,15,1); //Set 15 which is En7 on schematic to high to disable the bulk level shifters
            setPWM(SyncBoard_PWM_LVLShift_ADR,16,0); //Set 16 which is En8 on schematic to low to disable the 3V3 source to bulk level shifters (i.e. power net 3V3S)
    }
    gpiosEnabled = false;
}


void turnICsOff(){
    setPWM(SyncBoard_PWM_LVLShift_ADR, 0, 0.0); // Turn off the level shift PWM. Also sets direction parameters of GPIOs to output which is sub optimal but should be OK since they are also disabled.
    setPWM(SyncBoard_PWM_LVLShift_ADR, 15, 1); // Disables the outputs of the big level shifters. Note they should already be off at this point as their power supply is disabled above, but whatever.
    setPWM(SyncBoard_PWM_Switches_ADR, 0, 0.0); // Turn off the switches
    setupDACSPI(false); // Turn off the DACs
        // Note these are in essence a hard reset and rely on the fact that 0 on the outputs is off in our PCB
    if (LEDAttached){
        setPWM(LED_PWM_ADR, 0, 0.0); // Turn off the LED PWM if it is there. Note this sets LED currents to be the controller of everything.
        setupLEDDACI2C(false); // Turn on the DAC
    }
    if (MagAttached){
        setupMagDACI2C(false); // Turn off the DAC
    }
}


void configureIO (bool GPIOInput[9], int GPIOFunction[9], bool GPIOEnabled[9], bool activate = true){
  // Function to set up the IO pins, this can be called at any time to reconfigure the IO pins.
  // Note conceptually this is turning directions and setup config on/off, it isn't actually turning the pins on/off. It does, however, set a few default values e.g. LEDs all off.

  // If activate true then we are setting it up as we want it. If false we are going into safestate so we want to disable all the outputs.
  // GPIOInput is an array of 9 bools, one for each GPIO pin. True means input, false means output.
  // GPIOFunction is an array of 9 ints, one for each GPIO pin. 0 means GPIO, 1 means PWM, 2 means Analog Input (not implemented yet)
  // GPIOEnabled is an array of 9 bools, true for enabled.
  
    
  if (systemEnable && activate){ //If this is true then we are trying to reconfigure GPIOs while i is already running which is a nono.
    raiseError("System is already enabled, can't re-enable or configure it in this state.");
    return;
  }

      // Fist we set up heartbeat to make sure things are live for following.
    pinModeFast(Heartbeat, OUTPUT); // Set the heartbeat pin to output and then set its value to low
    digitalWriteFast(Heartbeat, LOW);
    delay(1);
    digitalWriteFast(Heartbeat, HIGH);
    delay(1);
    digitalWriteFast(Heartbeat, LOW);



    // FIrst we deal with generic all-purpose IO pins, that arent leve shifted.
  if (activate){ //The usual case when we want to run the system.

        pinModeFast(D_IN_1, INPUT); // Set the digital input pins to input
        pinModeFast(D_IN_2, INPUT);
        pinModeFast(D_IN_3, INPUT);
        pinModeFast(D_IN_4, INPUT);
        pinModeFast(D_OUT_1, OUTPUT); // Set the digital output pins to output
        pinModeFast(D_OUT_2, OUTPUT);
        pinModeFast(D_OUT_3, OUTPUT);
        pinModeFast(D_OUT_4, OUTPUT);
        digitalWriteFast(D_OUT_1, LOW); // Set the digital output pins to low
        digitalWriteFast(D_OUT_2, LOW);
        digitalWriteFast(D_OUT_3, LOW);
        digitalWriteFast(D_OUT_4, LOW);
  
        pinModeFast(Camera_LED1, INPUT); // Set the camera LED pins to input using the fast read library.
        pinModeFast(Camera_LED2, INPUT);
        pinModeFast(Camera_LED3, INPUT);
        pinModeFast(Camera_LED4, INPUT);
        pinModeFast(Camera_Trigger_ready, INPUT); // Set the camera trigger ready pin to input
        pinModeFast(Camera_Reading, INPUT); // Set the camera reading pin to input
 
  } else { //When we first turn things on and want to be noncomittal so enter a safe state.
    
        //FHere we just turn everthng off.
        for (int i = 0; i < 42; i++) {
        pinModeFast(i, INPUT); // Default setup to be fault tolernace high impedance
        // pinMode(i, INPUT); // Some default setup 
        }
   
    
  }
  
    // set up SPI bus
    
    SPI.begin();
    SPI.setMOSI(SPI_MOSI0);
    SPI.setMISO(SPI_MISO0);
    SPI.setSCK(SPI_SCK0);
    pinModeFast(SPI_CS0, OUTPUT);
    pinModeFast(SPI_CS1, OUTPUT);
    SPI.setBitOrder(MSBFIRST);
    
    

    // setup I2C bus
    Wire.begin();
    Wire.setClock(100000L); // Could make this 400khz if you want faster but may hurt integrity of i2c comms - we are running a bit of a big I2C bus with much capacitance.
    
    // Set up all the chips.
    setupPWMs();
    turnICsOff(); // Turn off all the I2C/SPI devices and more perhps. .

        // Now config the GPIO pins.
    if (activate){
                
        setupGPIO(GPIO_13, GPIOEnabled[0], GPIOFunction[0],GPIOInput[0]); // Set up the GPIO pins
        setupGPIO(GPIO_25, GPIOEnabled[1], GPIOFunction[1],GPIOInput[1]);
        setupGPIO(GPIO_26, GPIOEnabled[2], GPIOFunction[2],GPIOInput[2]);
        setupGPIO(GPIO_27, GPIOEnabled[3], GPIOFunction[3],GPIOInput[3]);
        setupGPIO(GPIO_28, GPIOEnabled[4], GPIOFunction[4],GPIOInput[4]);
        setupGPIO(GPIO_29, GPIOEnabled[5], GPIOFunction[5],GPIOInput[5]);
        setupGPIO(GPIO_30, GPIOEnabled[6], GPIOFunction[6],GPIOInput[6]);
        setupGPIO(GPIO_31, GPIOEnabled[7], GPIOFunction[7],GPIOInput[7]);
        setupGPIO(GPIO_32, GPIOEnabled[8], GPIOFunction[8],GPIOInput[8]);
    } else {

           //Now we can confiure all GPIOs to safe mode.     
        setupGPIO(GPIO_13, false);  // Set all GPIOs to input and 5V range to minimise any potential issues. Last input disables them through pwm
        setupGPIO(GPIO_25, false);
        setupGPIO(GPIO_26, false);
        setupGPIO(GPIO_27, false);
        setupGPIO(GPIO_28, false);
        setupGPIO(GPIO_29, false);
        setupGPIO(GPIO_30, false);
        setupGPIO(GPIO_31, false);
        setupGPIO(GPIO_32, false);

    }
    
      // Reset heartbeat in case needed.
    pinModeFast(Heartbeat, OUTPUT); // Set the heartbeat pin to output and then set its value to low
    digitalWriteFast(Heartbeat, LOW);
    delay(3);
    digitalWriteFast(Heartbeat, HIGH);
    delay(3);
    digitalWriteFast(Heartbeat, LOW);

    // Now we do need to set some things as output to ensure they are not floating. 
       
    pinModeFast(LED_1_Enable, OUTPUT); // Set the LED enable pins to output and then set their value to low
    digitalWriteFast(LED_1_Enable, LOW);
    pinModeFast(LED_2_Enable, OUTPUT);
    digitalWriteFast(LED_2_Enable, LOW);
    pinModeFast(LED_3_Enable, OUTPUT);
    digitalWriteFast(LED_3_Enable, LOW);
    pinModeFast(LED_4_Enable, OUTPUT);
    digitalWriteFast(LED_4_Enable, LOW);
    pinModeFast(LED_5_Enable, OUTPUT);
    digitalWriteFast(LED_5_Enable, LOW);
    pinModeFast(LED_6_Enable, OUTPUT);
    digitalWriteFast(LED_6_Enable, LOW);
    pinModeFast(LED_7_Enable, OUTPUT);
    digitalWriteFast(LED_7_Enable, LOW);
    pinModeFast(LED_8_Enable, OUTPUT);
    digitalWriteFast(LED_8_Enable, LOW);
    // Default DAC state (same as pullups/downs on board)
    pinModeFast(DAC_CLR_Bar, OUTPUT); 
    digitalWriteFast(DAC_CLR_Bar, HIGH); 
    pinModeFast(DAC_LDAC_Bar, OUTPUT);
    digitalWriteFast(DAC_LDAC_Bar, LOW);

    pinModeFast(Camera_Trigger_In, OUTPUT); // Set the camera trigger pin to output and then set its value to low
    digitalWriteFast(Camera_Trigger_In, LOW); // Set the camera trigger pin to output and then set its value to low
    


    

    if (!activate){// We turn off these pins again if we are doing things in safe mode.
        //Now disable the busses if we are not active. Note the reason we need this is because in the !activate command we want to set everything off on the I2C/PWM chips but if we didnt communicate them it wouldn't necessarily happen.
        disableGPIOandLevelShift(); // Disable the gpios and level shift so we can plug stuff in/out.

        SPI.end();
        Wire.end();
        //Set SPI/I2C back to input disable
        pinMode(SPI_CS0, INPUT_DISABLE);
        pinMode(SPI_CS1, INPUT_DISABLE);
        pinMode(SPI_MOSI0, INPUT_DISABLE);
        pinMode(SPI_MISO0, INPUT_DISABLE);
        pinMode(SPI_SCK0, INPUT_DISABLE);
        pinMode(I2C_SDA, INPUT_DISABLE);
        pinMode(I2C_SCL, INPUT_DISABLE);
        // Note IN THEORY the above business shouldn;t be necessary since if we are in here systemEnable is false and the heartbeat should be disabled. Thus the PWM chips should be powered down and everything would be off.
        Serial.println("Disabled GPIOs and level shifters");
    } else {
        enableGPIOandLevelShift(); // Enable the gpios and level shift if we are turning everything on. Note this should activate the NOT ready to connect indicator LED on the SyncBoard.
        setupDACSPI(true); // Turn on the DAC
        if (LEDAttached){  
            setupLEDDACI2C(true); // Turn on the DAC#
        }

        Serial.println("Enabled GPIOs and level shifters");
    }



}

void setSwitch(int channel = 0, float turnOn = 0.0){
    // used for setting the power switches on the sync board.
    // channel is from 1-16 with 0 meaning setting all at once
    // turnOn is a float, with 1.0 beign fully on and 0.0 being fully off.
    // Note this requires the I2C bus to be active!

    if (channel == 0 && turnOn >0.0){
        //this is if someone tries to turn everything on at once, which is not allowed
        raiseError("You tried to turn all the switches on at once. This is not allowed. Better luck next time.");
        return;
    }
 
    setPWM(SyncBoard_PWM_Switches_ADR, channel, turnOn);//set the given channel to on.

}

void debugIO(int channel, float value){
    calibrateLED(channel,10);
    return;
    
    
    // Below code is various debugging test codes that may no longer work so be careful man. 
    // digitalWriteFast(LED_8_Enable, LOW); // Enable the LED
    // // delay for 1ms
    // delay(5);
    // //First set the mode for that LED channel.    
    // // setLEDFeedbackSignal(channel, true); // FOr this channel set it to current feedback (true)
    // setLEDFeedbackSignal(channel, false); // FOr this channel set it to optical feedback (false)
    // delay(5);
    // setLEDVoltageOutput(channel, true); // Also set so that it will be readin gout current from ADC.
    // setLEDDACI2C(channel, value); //Now set the voltage to be given by the user. Should be between 0 and 3.3V. 
    
    // digitalWriteFast(LED_8_Enable, HIGH); // Enable the LED
    // delay(100); // Wait for 100ms
    // float adcval = readADC(channel,1); // This is just a dummy function to make sure the ADC is working. It is not used for anythi??ng else.
    // //Now we want to do 100 ADC reads then average them out to give a more accurate reading.
    // float sum = 0.0;
    // for (int i = 0; i < 100; i++){
    //     sum += readADC(channel,1);
    // }
    // adcval = sum/100.0;
    // float currentMeasured =((adcval-0.3) / (0.003*50.0))*1000.0; // This is the current in mA. The 0.003 is the sense resistor, the 50 is the gain and the 1000 is to convert to mA.
    // float currentAsk = ((value-0.294) / (0.003*50.0))*1000.0; // This is the current in mA. The 0.003 is the sense resistor, the 50 is the gain and the 1000 is to convert to mA.
    // float voltagediff = (1000.0*(adcval-0.3)); // This is the voltage drop across the sense resistor in mV.

    // //Now measure optical power
    // setLEDVoltageOutput(channel, false); // Also set so that it will be readin gout current from ADC.
    // sum=0.0;
    // for (int i = 0; i < 100; i++){
    //     sum += readADC(channel,1);
    // }
    // float adcval2= ((sum/100.0)-0.3)*1000.0; // This is the optical power in mV. The 0.3 is the offset and the 1000 is to convert to mV

    // // float voltagedrop = currentMeasured*51.0; // This is the voltage drop across the load resistor in mV - assuming there is a 51ohm resistor in place of LED!
    // // digitalWriteFast(LED_8_Enable, LOW); // Disable the LED
    // // Serial.println("You are asking for current of " + String(currentAsk) + "mA. ADC value is "+String(adcval) + "V which is a sense resistor voltage of " + String(voltagediff) + " mV,  which means current of "+String(currentMeasured)+" mA" + " for voltage of "+String(voltagedrop)+" mV across load");
    
    // // delay(5000); // Wait for 1s
    // digitalWriteFast(LED_8_Enable, LOW); // Enable the LED
    // Serial.println("You are asking for current of " + String(currentAsk) + "mA. ADC value is "+String(adcval) + "V which is a sense resistor voltage of " + String(voltagediff) + " mV,  which means current of "+String(currentMeasured)+" mA. Also optical power is " + String(adcval2) + "mV");

}