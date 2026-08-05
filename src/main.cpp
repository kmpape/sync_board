// It is worth saying that throughout this codebase variable names are almost randomly chosen as camelCase or _underscored_, there is no meaning or reason to interpret which ever is being used in a given context. sorry

#include <Arduino.h>
#include "MemoryController.h"
#include "SerialController.h"
#include "PinOut.h"
#include "GlobalVariables.h"
#include "IOController.h"
#include "digitalWriteFast.h"
#include "LEDDriver.h"
#include "ICDrivers.h"
#include "MagnetDriver.h"

enum SIGNALMODE {
  ADC = 0,
  DAC = 1,
  GPIO_READ = 2, // NOT IMPLEMENTED
  MAGDAC = 3,
  MAGADC = 4,
  MAGHALL_READ = 5,  // read the hall sensor in mT
  MAGCURR_WRITE = 6, // set the magnet current calibrated to 0
  MAGHALL_WRITE = 7, // set the magnet current in mT based on Hall calibration
  DO = 8,
  CONDUCTOR = 9,  // This is a signal that is used to conduct other signals.
  LED = 10,       // limited to lower powers 
  LED_TIMED = 11, // allows for higher power
  DO_TIMED = 12,
  GPIO_WRITE = 13,
};
u_int SIGNALMODES = 14; // Remember to update this if updating above enum

// Debug mode and various parameters
bool debugmode = true; // Set to true to enable debug mode. This will do some extra things. false for normal operation.
unsigned int debugtime = 0;


// Device Config 
bool systemEnable = false; // Flag to indicate if the system is enabled or not. When set true the system will be able to do things e.g. IOs and heartbeat are on.
// Note any time that we have the system disabled we should also have the IO disabled. This is because the IO is used to control the system.

bool LEDAttached = false; // Flag to tell if the LED board is connected. True would mean we HAVE connected the LED driver. Note there are more LED parameters below for timing purposes.
bool MagAttached = false; // Flag to tell if the magnet board is connected. True would mean we HAVE connected the magnet driver.

// Values used in serial comms
String commandString;

// Values used in timing 
bool systemBusy = false; // CURRENTLY NOT USED. Flag to indicate if the system is busy executing time sensitive tasks.
bool systemExposing = false; // Flag to indicate if the system is currently exposing an image.



///// Sync mode parameters
int syncMode = 0; // 0 = system is not syncing with external devces, 1=PC drives syncBoard, 2=PC drives device drives syncBoard
// Common mode parameters.
const int maxImages = 4; // Maximum number of images that are collected in a single sync cycle. If you change this you will need to change decoding of setupImageSequence
bool modeImageActive[maxImages] = {false}; // Flag to indicate if that image ID is expected to be collected. 
int modeLEDChoice[maxImages] = {0}; // Which LED to use for each image. 0=none, 1=LED1, 2=LED2, 3=LED3, 4=LED4 etc. The reason we have this separate from modeImagesActive is so you can take an image with no LEDs on. 
bool modeLEDSwitchedByCamera = false; // Flag to indicate if the LED turn on is controlle by the camera trigger or by the PC/SyncBoard. True=set by camera, false=timer on syncboard turns it off when needed. Note if you set timers in modeExposureTime it willl override this.
unsigned int modeExposureTime[maxImages] = {0}; // Exposure time for each LED in micro seconds. Note ven if modeLEDSwitchedByCamera is true if you have non zero values here it will override it.
// bool modeLEDOpticalControl[maxImages] = {false}; // Flag to indicate if the LED intensity is controlled by optical feedback (true) or current feedback (false)
// float modeLEDIntensityOptical[maxImages] = {0.0}; // LED intensity for each LED when in optical mode.
// float modeLEDIntensityCurrent[maxImages] = {0.0}; // LED intensity for each LED when in current mode. We separate these to prevent confusion and unexpected intensities.
bool modeFilterWheelChange[maxImages] = {false}; // Flag to indicate if the filter wheel needs to do something for this image.
bool modeDMDChange[maxImages] = {false}; // Flag to indicate if the DMD needs to do something for this image.
bool modeOtherChange[maxImages] = {false}; // Flag to indicate if other things need to do something for this image.  E.g. Magnet or GPIO pins.
int currentImageID = -1; // The current image ID we are on. This is incremented each time we take an image. Starting point is -1 (so we increment once to get to 0)
unsigned long exposureStartTime = 0; // The time we started exposing the current image. This is used to check if we have finished exposing.
unsigned long sequenceStartTime = 0;
unsigned long sequenceTimeout = 10000000; // If we dont finish the sequence in this time in microseconds (10 seconds) then we have a problem.
bool sequenceRunning = false;
bool cameraTriggering = false; // True if we have set the camera trigger to high but not yet back to low. This is used to prevent us from triggering the camera again while it is still exposing.
int cameraTriggeringStarttime = 0; // The time we started triggering the camera in microseconds. This is used to check if we have finished triggering.
const int cameraTriggeringTimeout = 1000; // How long we hold camera trigger HIGH for in microseconds. Shouldnt be changed, just needs to be long enough for the camera to see it.
//Mode 1 specific parameters
//None
//Mode 2 specific parameters
int mode2TriggerSignal = Camera_LED1; //Note here we are assuming that mode2 is going to get triggered by camera LED1 signal.

//// External Triggers
// Note you need to set up these triggers as pinModeFast in IOController!!
const int numTriggers = 5; // How many triggers are there.
int triggers[numTriggers] = {Camera_LED1, Camera_LED2, Camera_LED3, Camera_LED4, mode2TriggerSignal}; // List of pins that may be used as external triggers - ORDER IS IMPORRTANT! If you want to change this stuff you need to change triggerHandler.
int prevTriggerState[numTriggers];




///// IO parameters
// Set up GPIO states
bool GPIOInput[9]={true}; // False = output, true = input. Default is true as high impedance.
int GPIOFunction[9]={0}; // 0 = GPIO, 1 = PWM, 2 = Analog Input (not implemented yet)
bool GPIOEnabled[9] = {false};


///// Timing functionalities
//heartbeat
const int heartbeatTimeout = 100; // How often we want the heartbeat to pulse in millis
int heartbeatTriggerTime = 0; // When we last flipped the heartbeat.

// Structure for dealing with various signals that require timed actions. Examples are ADC recording, DAC writing, GPIO toggling and so on.
const int numSignals = 5; // How many timed signals are there.
int signalActive[numSignals] = {false}; // Flag to indicate if that signal is active.
bool signalRepeat[numSignals] = {false}; // If you should make it repeat. This works for mode 0 but not for mode 1...
int signalMode[numSignals] = {0}; // 0 = ADC Recording, 1 = DAC Writing, 2 = GPIO doing something? 
uint32_t signalOptions[numSignals] = {0}; // Options for the signal. For ADC this is the channel, for DAC this is the pin, for GPIO this is something else. For conductor this is a bit mask of which other signals to call.
bool signalIsSlave[numSignals] = {false}; // If this signal is a slave to another signal. This means it will only run if the master signal is active.

int signalTimeout[numSignals] = {0}; // How long to next action in microseconds. Note leave this at 0 if it is not being used.
int signalTriggerTime[numSignals] = {0}; // When we last flipped the signal in microseconds.
// The current position in the buffer. This is incremented each time we add a new value. Starts at -1 so we can begin with new value at 0
// DO NOT initialise this as {-1}! In C that only sets element 0 to -1 and zero-fills the rest, which leaves
// signals 1..N starting one step ahead of signal 0. Everything here is filled properly in initSignalBuffers().
int signalPosition[numSignals];

const int signalMaxLength = 2000; // How long each buffer is. You are making even the shorter ones this long but yolo.
float signalData[numSignals][signalMaxLength]; // The data buffers. Note this could be used to store values (ADC mode) or record values (DAC mode).
int signalTiming[numSignals][signalMaxLength]; // The timing buffers in microseconds if needed - says when actions should be done. -1 means you stop there. Same initialisation trap as above, so also done in initSignalBuffers().


///// LED parameters - mostly defined in LEDDriver.cpp.
int NumberLEDsBeingTimed = 0; // Flag to indicate if at least one LED is on and being timed, used in the triggering sectionof the code here. We try to handle all the LED stuff in its driver
// But include this here for syncronisation purposes.


///// DO parameters
int DOTimeout[4] = {0}; // Array to store the timeout for each DO
int DOTriggerTime[4] = {0}; // Array to store the trigger time for each DO
int NumDOBeingTimed = 0; // Number of DOs being timed

// Handles triggers that need to do a pulse,
// e.g. the camera which uses a rising edge
void DOTimingHandler() {
  for (int i = 0; i < 4; i++) {
    if (DOTimeout[i] > 0) { // This should be non-zero if the DO is being timed
      if (micros() - DOTriggerTime[i] > DOTimeout[i]) {
        digitalWriteFast(D_OUT_1 + i, LOW); // Turn off the DO
        DOTimeout[i] = 0; // Reset the timeout
        NumDOBeingTimed--; // Decrement the number of DOs being timed
      }
    }
  }
  if (NumDOBeingTimed < 0) {
    raiseError("Number of DOs being timed is negative");
  }
}

void switchDOTimed(int DO, int time) {
  // DO = 1, 2, 3, 4
  // time ON in milliseconds
  if (DO < 1 || DO > 4) {
    raiseError("DO number out of range");
    return;
  }
  if (time < 0) {
    raiseError("Time out of range");
    return;
  }
  if (DOTimeout[DO - 1] > 0) {
    raiseError("DO already being timed");
    return;
  }
  digitalWriteFast(D_OUT_1 + DO - 1, HIGH); // Turn on the DO
  DOTimeout[DO - 1] = time * 1000; // Set the timeout
  DOTriggerTime[DO - 1] = micros(); // Record the time we triggered the DO
  NumDOBeingTimed++; // Increment the number of DOs being timed
}


void initSignalBuffers(){
  // Puts every signal into the "never run yet" state. Called once from setup() so that the very first
  // sequence after a power cycle / reset / reflash starts from index 0 on ALL signals, not just signal 0.
  for (int i=0; i<numSignals; i++){
    signalActive[i] = false;
    signalPosition[i] = -1;
    signalTriggerTime[i] = 0;
    signalTimeout[i] = 0;
    for (int j=0; j<signalMaxLength; j++){
      signalData[i][j] = 0.0;
      signalTiming[i][j] = -1; // -1 means "stop here", so an un-setup signal can never free-run at 0us.
    }
  }
}


void factoryReset(){
  // Hard reset of memory (and maybe other things). Don't run unless you mean it!
  clearMemory(); // Set all EEPROM byte+float values to 0
}


void imageHardReset(){
  // This function is called when we want to do an unexpected reset of the imaging system. It is called by the disable system or if imagesquence times out
  sequenceRunning = false; //Indicates we have ended an image sequence.
  currentImageID = -1; // Reset the image ID so we could in theory go again.
  // And reset some more stuff
  systemExposing = false; 
  digitalWriteFast(Camera_Trigger_In, LOW); // Turn off the camera trigger pin.
  cameraTriggering = false;
}




void enableSystem(bool enable = false){
  // Function to enable or disable the system. This is used to prevent accidental changes to the system while it is running.
  // This is the only place you should change the value of systemEnable.
  if (enable){
    configureIO(GPIOInput, GPIOFunction, GPIOEnabled, true); // First configure the IO pins
    // Next record the inivial values of triggers so we can detect changes later
    for (int i=0; i<numTriggers; i++){
      prevTriggerState[i] = digitalReadFast(triggers[i]);
    }
    if (LEDAttached){
      resetLEDs(); // Reset the LED driver to default settings. You need to do this to config the hardware on that board! 
    }

    // delay(100); // Delay so there is some time for heartbeat to get its act together. I think this isn't needed since all digiatl comms shouldn't rely on it.
    systemEnable = true; // Don't try to set this anywhere other than here!
    Serial.println("System enabled");
  } else { //If we are disabling the system the below does various hard-coded things to turn off different parts. Many of these are inefficient double ups but we have them anyway to make the code.... less likely to break in future maybe?
    //First do things quickly that need to be turned off. 
    if (systemEnable){ // In this case we ALREADY have the system on but are turning it off. In such a case we therefore quickly turn off various IOs 
      turnICsOff(); // Turn off the ICs. Note this is again called in configureIO but we do it here to make sure it happens qquickly.
      turnLEDsOff();
      digitalWriteFast(D_OUT_1, false); //Turn off digital outputs - though again this is done in configureIO
      digitalWriteFast(D_OUT_2, false);
      digitalWriteFast(D_OUT_3, false);
      digitalWriteFast(D_OUT_4, false);
      NumberLEDsBeingTimed = 0; // Reset this flag since we are turning off the system.
    }
    for (int i=0; i<numSignals; i++){// Disable our active signals.
        // Wind them back to the start too. Without this an aborted run (e.g. the PC script crashes or is
        // Ctrl-C'd before it sends stopSignal) leaves the positions mid-sequence, and the next run then
        // starts its slaves part way through their buffers - the output pattern comes out rotated.
        signalActive[i] = false;
        signalPosition[i] = -1;
        signalTriggerTime[i] = 0;
        signalTimeout[i] = 0;
    }
    //Disable our active images. Note we do this here but NOT in reset() since we have immediately above disabled any LEDs and peripherals so this couldn't accidentally leave a LED on. 
    imageHardReset(); // Reset the imaging system. This is the hard-reset function that can be called here and also in the timeout case. Otherwise it should never be needed as propery execute image sequences do all this stuff automatically.
    //Note we dont disable the GPIOs here since that will be done next in ConfigureIO. We also dont set the various digital IO pins (aside from those above) since we will do this quickly in configureIO.
    
    configureIO(GPIOInput, GPIOFunction, GPIOEnabled, false); //Note with the false flag this ignores the GPIO arguments.


    if (LEDAttached){
      resetLEDs(false); // Reset the LED driver back to default settings. We put false flag here since the hardware will be disabled at this point so we shouldn't be trying to talk to it on i2C as it wont get through...
    }

    delay(1000); // Delay so there is some time for heartbeat to shut down
    systemEnable = false; // Don't try to set this anywhere other than here!
    Serial.println("System disabled");
  } 
}




void resetConfig(){
  // Soft reset of all configuration parameters. Should be equivalent to turning it off and on again.
  // Note this is a reset of the DATA state. If you want to reset IOs and electronics you need to disable then enable system.
  
  enableSystem(false); // First disable the system as we dont want to do this if it is running heh.
  //FIrst the imaging parameters
  
  for (int i=0; i<maxImages; i++){// We wont reset systemExposing and or sequenceRUnning or currentImageID to avoid bugs (maybe?)
    modeImageActive[i] = false;
    modeLEDChoice[i] = 0;
    modeExposureTime[i] = 0;
    modeFilterWheelChange[i] = false;
    modeDMDChange[i] = false;
    modeOtherChange[i] = false;
  }

  // Now the GPIO parameters
  for (int i=0; i<9; i++){
    GPIOInput[i] = true;
    GPIOFunction[i] = 0;
    GPIOEnabled[i] = false;
  }

  // Now the timed signals
  for (int i=0; i<numSignals; i++){
    signalMode[i] = 0;
    signalActive[i] = false;
    signalTimeout[i] = 0;
    signalTriggerTime[i] = 0;
    signalPosition[i] = -1;
    for (int j=0; j<signalMaxLength; j++){
      signalData[i][j] = 0.0;
      signalTiming[i][j] = -1;
    }
  }

  resetLEDs(); // Reset the LED driver to default settings. Note this is a function in LEDDriver.cpp
  NumberLEDsBeingTimed = 0; // Reset this flag since we are turning off the system.
}




void imageLEDControl(int imageID, bool STATE = false){
  // This function is called when we want to control the LED for an image. It is called from the main loop EITHER by a trigger
  // imageID is which image in the sequence
  // STATE is a flag to indicate if we want to turn the LED on (true) or off (false).

  if (modeLEDChoice[imageID] == 0){ // If we arent using an LED for this image then we dont need to do anything.
    return;
  }
  if (modeLEDChoice[imageID] > 8){ // If we are using an LED that doesnt exist then we have a problem.
    raiseError("LED choice not recognised");
    return;
  }
  if (LEDAttached == false){ // If we dont have an LED attached then we have a problem.
    raiseError("No LED system attached but you are trying to turn on LEDs for imaging!");
    return;
  }
 
  switchLEDDirect(modeLEDChoice[imageID], STATE, true); // Turn on (or off) the LED. Note the last argument is true to override the safety timer on LED intensity.
}

void startImage(){
  // This function is called when we want to start one image cycle. It does any config and triggering needed.
  // Note if we are using modeLEDSwitchedByCamera it will NOT turn on the LEDs since that should be done by triggers.

  if (systemExposing == true){
    raiseError("Cant start new image while the previous one is being exposed. Your combinations of timings and triggers is leading to some form of overlaps...");
    return;
    }


  currentImageID++; //Incremnt our image index. Note it gets reset to -1 so first time through it goes to 0

  if (modeFilterWheelChange[currentImageID]){ // If we need to change the filter wheel then we do it here.
    //todo: Do something
  }

  if (modeDMDChange[currentImageID]){ // If we need to change the DMD then we do it here.
   //todo: Do something
  }

  if (modeOtherChange[currentImageID]){ // If we need to change other things then we do it here.
    //todo: Do something
  }

  if (modeLEDSwitchedByCamera == false){ // If we arent relying on the camera to tell us when to turn on LED we do it right away when we start imaging.
    imageLEDControl(currentImageID, true); // second argument means we are turning it on.
  }
  
  

  if (syncMode == 1){
    bool cameraReady = digitalReadFast(Camera_Trigger_ready); // Check if the camera is ready to take an image.
    if (cameraReady == false){ // If the camera isnt ready then we have a problem.
      raiseError("You tried to trigger the camera before it was ready. Maybe we should have a wait statement here?");
    } else if (systemExposing == false) { //Note this requires exposing = false i.e. we only do it at the start of the exposure sequence. You could remove this to make it trigger with every image in sequence...
      digitalWriteFast(Camera_Trigger_In, HIGH); // Trigger the camera
      cameraTriggering = true; // Note we have triggered the camera but not yet turned it off.
      cameraTriggeringStarttime = micros(); // Record the time we started triggering the camera.  
    }
    
  } else if (syncMode == 2){
    //todo: Maybe we also need to trigger the camera here if some other device (not camera) is triggering the whole system.
  }
  
  exposureStartTime = micros(); // Record the time we started exposing the image.
  systemExposing = true; //Note in Mode1 we have triggered above so it should be exposing. In Mode 2 we should also have been triggered so the camera should be going.
  
  
}

void endImage(){
  // This function is called when we want to end taking an image. It is called from the main loop EITHER by a trigger or serial commabd,
  // imageID is which image in our sequence of settings it is we are taking.  
  imageLEDControl(currentImageID, false); // Turn off the LED - note sometimes it might already be off if we are using modeLEDSwitchedByCamera since a trigger might have done it.
  
  if (systemExposing == false){
    raiseError("Cant end image while not exposing. You have messed up somehow.");
    return;
  } 
  systemExposing = false;

}



void imageSequence(bool starting = false, bool triggerLED = false, bool triggerLEDState = false, int triggerLEDID = 0){
  // This function is called repetitively when we are taking a sequence of images. It can be called three ways: Triggered, serial command, or every time main loop runs. 
  // starting is a flag to say if we are calling imageSequence for the first time (i.e. we should start things off). 
  // triggerLED is to say that this has been called with a LED trigger input
  // triggerLEDState is whether that trigger is saying to turn the LED off or on.
  // triggerLEDID is which LED trigger it is. Note this is not the LED CHANNEL on the driver but which in the sequence of LEDs for the imaging setup
  
  if (starting){ // If we are starting a new sequence 
    if (currentImageID != -1){
      raiseError("Cant start new image sequence while we are already in one. You have messed up somehow.");
      return;
    }
    startImage(); // Start the image 
    sequenceStartTime = micros(); // Record the time we started the sequence in microseconds.
    sequenceRunning = true; //Indicates we have started an image sequence.
  }

  bool finished_exposing = false;

  if (triggerLED){ //to improve execution speed this block only happens when we get a trigger commabnd.
    if (modeLEDSwitchedByCamera && triggerLEDState == true){ //If the camera says to turn on the LED.
      if (!systemExposing){
        raiseError("Camera says to turn on LED but we havent started startImage(). You have messed up somehow.");
        return;
      } else if (triggerLEDID != currentImageID){
        raiseError("Camera says to turn on LED number " + String(triggerLEDID) + " in your imaging sequence but we are on image " + String(currentImageID) + ". You have messed up somehow.");
        return;
        }
      imageLEDControl(triggerLEDID, true); // second argument means we are turning it on.
      Serial.println("got LED ON Trigger");

    } else if (modeLEDSwitchedByCamera && triggerLEDState == false){ //if camera says turn off LED
      if (triggerLEDID != currentImageID){
        raiseError("Camera says to turn off LED number " + String(triggerLEDID) + " in your imaging sequence but we are on image " + String(currentImageID) + ". You have made frames overlap somehow.");
        return;
        }
      finished_exposing = true;
      imageLEDControl(triggerLEDID, false); // second argument means we are turning it off
    }
  }


  if (modeLEDSwitchedByCamera == false || modeExposureTime[currentImageID]>0){ //Means we are timing it ourselves i.e. not relying on LED triggers to tell us when to act.
    // Note if you have set an modeExposureTime nonzero then timers will also turn off your LED!
    if (micros() - exposureStartTime > modeExposureTime[currentImageID]){ // If we have been exposing for long enough then we are done.
      finished_exposing = true;
      Serial.println("Timed out exposure");
    }
  }



  if (finished_exposing){ //In this case we've decided that this image (and maybe the whole sequence) needs to end.
    endImage(); // End the image. Note if we have modeLEDSwitchedByCamera==true this LED migth ALREADY be off if we triggered it, but it might be on if we were uing timers. 
    Serial.println("Ended Exposure");
    // Now we need to decide if we end the sequence or start a new image.
    if (currentImageID == maxImages-1) { // If we have gotten to the maximum number of images possible we stop.
      sequenceRunning = false; //Indicates we have ended an image sequence.
      currentImageID = -1; // Reset the image ID for next time.
    } else {
      if (modeImageActive[currentImageID+1] == true){ // If there is a next image setup. Note this wont turn on LEDs in modeLEDSwitchedByCamera as we are still awaiting the trigger.
        startImage(); // Start the next image. 
        Serial.println("Started a subsequent image.");
      } else { // If the next image is not active then we want to stop.
        sequenceRunning = false; //Indicates we have ended an image sequence.
        currentImageID = -1; // Reset the image ID for next time.
        Serial.println("Ended image sequence which took " + String(micros() - sequenceStartTime) + " microseconds.");
      }
    } 
  }

  if (cameraTriggering){ // This turns off camera triggersignal after appropriate time. Note it is started in startImage but only the first time it is called in an imageSequence
    if (micros() - cameraTriggeringStarttime > cameraTriggeringTimeout){ // If we have reached the time required for triggering.
      digitalWriteFast(Camera_Trigger_In, LOW); // Turn off the camera trigger pin.
      cameraTriggering = false;
    }
  }
  
  // This is an error handling case where your whole thing times out, should never happen in practice.
  if (micros() - sequenceStartTime > sequenceTimeout){ 
    endImage();
    enableSystem(false); //Shut down stuff in case this is some cause for alarm. Sorry not sorry.
    imageHardReset(); // Reset the imaging system.
    raiseError("Image sequence timed out. You have messed up somehow, maybe with bad timings or maybe a trigger was missed.");
    return;
  }
  
}



void triggerHandler(int triggerID, int currentState){
  // This function is called when a trigger is detected. It is called from the main loop.
  // currentState is the state of the trigger i.e. high if that logic signal is higb.
  
  // First the imaging triggers for Sync modes 1 and 2
  
  if (triggerID >= 0 && triggerID <= 3){ // This is the case of one of our LED triggers coming from camera.
    if (modeLEDSwitchedByCamera == true){ // If we are relying on the camera to tell us when to turn on the LED. Otherwise we dont care about this.
      imageSequence(false, true, currentState, triggerID); // Call the sequence handler. 3rd flag means we are telling it this is a trigger, 4th flag means we are telling it the state of the trigger.

    }

  } else if (triggerID==4){ // Mode 2 trigger - means we are starting a new image sequence toggled by this 
    if (syncMode == 2 && currentState == true){ // If we are in sync mode 2 then we are waiting for this trigger to start a new image sequence if it was a low-high transition.
      imageSequence(true); // Start a new image sequence.
    } else {
      // nothing
    }
  } else {
    raiseError("TriggerHandler error - not recognised trigger");
  }
}



void signalStopAndBackToStart(int signalIndex){
      //Function to call when you want a signal to end and get set back to a state that it could be restarted.
      //Note this doesnt reset anthing (e.g. data or timing buffers)
      signalActive[signalIndex] = false; // Flag to indicate that the signal is no longer active.
      signalPosition[signalIndex] = -1; // Reset the position to the start of the buffer so it can run again.
      signalTriggerTime[signalIndex] = 0; // Reset the trigger time to 0 so it can run again.
      signalTimeout[signalIndex] = 0; // Reset the timeout to 0 so it can run again.

      // If it is the conductor signal then need to deactivate the other signals.
      if (signalMode[signalIndex] == SIGNALMODE::CONDUCTOR){
        for (int i=0; i<numSignals; i++){
          if ((signalOptions[signalIndex] & (1 << i)) && signalIsSlave[i]) { // If the bit is set then we need to deactivate the signal.
            signalStopAndBackToStart(i);
          }
        }
      }
}

void signalResetAndStart(int signalIndex){
    //Function to call when you want a signal to reset and start from the beginning.
    //Note this doesnt reset anthing (e.g. data or timing buffers)
    signalPosition[signalIndex] = -1; // Reset the position to the start of the buffer so it can run again.
    signalTriggerTime[signalIndex] = 0; // Reset the trigger time to 0 so it can run again.
    signalTimeout[signalIndex] = 0; // Reset the timeout to 0 so it can run again.
    // If it is a ADC signal we need to reset the data values - note this SHOULDN'T be required but it is here just in case.
    if (signalMode[signalIndex] == SIGNALMODE::ADC){ // If we are recording ADC
      for (int i=0; i<signalMaxLength; i++){
        signalData[signalIndex][i] = 0.0;
      }
    }
    signalActive[signalIndex] = true; // Flag to indicate that the signal is active.

    // If it is the conductor signal then need to activate the other signals.
    if (signalMode[signalIndex] == SIGNALMODE::CONDUCTOR){
      for (int i=0; i<numSignals; i++){
        if ((signalOptions[signalIndex] & (1 << i)) && signalIsSlave[i]) { // If the bit is set then we need to activate the signal.
          // Wind the slave back to the start as well - this MUST mirror signalStopAndBackToStart. If you only
          // set signalActive here then a slave resumes from wherever it was left, so it runs at a different
          // buffer index to its siblings and everything it drives comes out shifted relative to them.
          signalPosition[i] = -1;
          signalTriggerTime[i] = 0;
          signalTimeout[i] = 0;
          signalActive[i] = true;
        }
      }
    }
}

void signalHandler(int signalIndex){
  //This function to handle signals, it called from main loop if signal is active.
  // The fact that this is getting called implies that we have reached the signalTriggerTime set for the next transition.
  //Note we should already be confident that signlIndex is in range given how it is called.
  
  bool stoppingSignal = false; // Flag to indicate if we are stopping the signal.

  //We start the time for the NEXT conversion now so that we aren't introducing an extra delay due to the time it takes the code below to run.
  signalTriggerTime[signalIndex] = micros();

  //First we increment our index on the signal
  int index = signalPosition[signalIndex] + 1; // Calculate new index which says what location along that signal we are up to.

  //now decide what to do next
  if (index >= signalMaxLength  || signalTiming[signalIndex][index]==-1 ){ // If we have reached the end of the buffer or if next timer indicates we should wrap.
    if (signalRepeat[signalIndex]){ // If we are repeating the signal there are various things we need to do, and this depends on the mode.
      if(signalMode[signalIndex] == SIGNALMODE::ADC){ // If we are recording ADC
          //The way we repeat here is shuffle all the values in signalData back by 1 index.
          for (int i=0; i<signalMaxLength-1; i++){ //Todo: Is this a good way to do things? It might be quite slow. 
            signalData[signalIndex][i] = signalData[signalIndex][i+1];
          }
          index = index-1;//We then subtract 1 from index so in following code it will rewrite over the last value in the array (or whatever the last value was in timings)
      
      } else { //DAC and GPIO modes just return to start and rerun the same output.
          index = 0; // Reset the position to the start of the buffer. Note we set it to 0 (rather than -1) since it needs to be used in the following code to start the new state. 
      }
      
    } else { // If we reached the end but ARENT supposed to repeat then we need to stop the signal.
      stoppingSignal = true; // Flag to indicate we are stopping the signal.
    }
  }
  
  //If we decided to stop the signal we can end here
  if (stoppingSignal){
    signalStopAndBackToStart(signalIndex);
    return;
  } 
  
  //Now do the action required depending on mode.
  if (signalMode[signalIndex] == SIGNALMODE::ADC){ // If we are recording ADC
      float adcValue = readADC(signalOptions[signalIndex]); // Read the ADC value of the specified pin in signalOptions
      signalData[signalIndex][index] = adcValue; // Record the ADC value in the buffer.
  } else if (signalMode[signalIndex] == SIGNALMODE::MAGADC) {
      float adcValue = readADC(signalOptions[signalIndex], 2); // Read the ADC value of the specified pin in signalOptions
      signalData[signalIndex][index] = adcValue; // Record the ADC value in the buffer.
  } else if (signalMode[signalIndex] == SIGNALMODE::MAGHALL_READ) {
      float hallValue = singleReadHall(signalOptions[signalIndex]); // Read the ADC value of the specified pin in signalOptions
      signalData[signalIndex][index] = hallValue; // Record the ADC value in the buffer.
  } else if (signalMode[signalIndex] == SIGNALMODE::DAC){ // If we are writing DAC
    setDAC(signalOptions[signalIndex], signalData[signalIndex][index]); //Set the DAC value of the specified pin in signalOptions to the value in the signalData.
  } else if (signalMode[signalIndex] == SIGNALMODE::GPIO_READ){ // If we are doing GPIO
    //Todo something if you want to implement signals to GPIO ports.
    raiseError("GPIO read not implemented yet");
  } else if (signalMode[signalIndex] == SIGNALMODE::GPIO_WRITE){ // If we are doing GPIO
    digitalWriteFast(signalOptions[signalIndex], signalData[signalIndex][index]);
  } else if (signalMode[signalIndex] == SIGNALMODE::MAGDAC){
    setMagDACI2C(signalOptions[signalIndex], signalData[signalIndex][index]); //Set the DAC value of the specified pin in signalOptions to the value in the signalData.
  } else if (signalMode[signalIndex] == SIGNALMODE::MAGHALL_WRITE){
    setMagnetField(signalOptions[signalIndex], signalData[signalIndex][index]); //Set the DAC value of the specified pin in signalOptions to the value in the signalData.
  } else if (signalMode[signalIndex] == SIGNALMODE::MAGCURR_WRITE){
    setMagnetCurrent(signalOptions[signalIndex], signalData[signalIndex][index]); //Set the DAC value of the specified pin in signalOptions to the value in the signalData.
  } else if (signalMode[signalIndex] == SIGNALMODE::DO){
    digitalWriteFast(signalOptions[signalIndex], signalData[signalIndex][index]);
  } else if (signalMode[signalIndex] == SIGNALMODE::LED) {
    // switchLEDDirect(signalOptions[signalIndex], signalData[signalIndex][index] > 0.0);
    switchLED(signalOptions[signalIndex], signalData[signalIndex][index] > 0.0);
  } else if (signalMode[signalIndex] == SIGNALMODE::LED_TIMED) {
    if (signalData[signalIndex][index] > 0)
      switchLEDTimed(signalOptions[signalIndex], signalData[signalIndex][index], true);
  } else if (signalMode[signalIndex] == SIGNALMODE::DO_TIMED) {
    if (signalData[signalIndex][index] > 0)
      switchDOTimed(signalOptions[signalIndex], signalData[signalIndex][index]);
  } else if (signalMode[signalIndex] == SIGNALMODE::CONDUCTOR){
    // If we are in conductor mode we need to trigger other signals. We do this by setting the signalOptions to a bit mask of which signals to trigger.
    for (int i=0; i<numSignals; i++){
      if ((signalOptions[signalIndex] & (1<<i)) && signalActive[i] && signalIsSlave[i]) { // If the bit is set then we trigger that signal.
        signalHandler(i);
      }
    }
  } else {
    raiseError("SignalHandler error - not recognised signal mode");
  }

  //Finally we set up for next period.
  signalTimeout[signalIndex] = signalTiming[signalIndex][index]; // Calculate how long to next action in microseconds.
  signalPosition[signalIndex] = index;
  
  
}



void executeSerialCommand(String command, String commandString){
  // This function decides what to do with various serial commands. It is called from the main loop.
  
  if (command == "factoryReset"){
              //clears the EEPROM so use with caution.
              factoryReset();

  } else if (command == "resetConfig"){
              //resets the system to default values - soft reset should be equivalent to turning it off and on
              resetConfig();

  } else if (command == "attachLED"){
              //Attaches the LED driver. This is called when the LED driver is connected and we want to start using it.
              if (systemEnable == true){
                raiseError("Cant attach LED while system is enabled");
                return;
              }
              bool LEDpresent = argGetBool(commandString,0); // If the LED is present or not.
              LEDAttached = LEDpresent; //Set to value of whatever the user sent.
              serialSend(command, LEDAttached);

  } else if (command == "attachMagnet") {
              // Attaches the Magnet driver. This is called when the Magnet driver is connected and we want to start using it.
              if (systemEnable == true){
                raiseError("Cant attach Magnet while system is enabled");
                return;
              }
              bool MagnetPresent = argGetBool(commandString,0); // If the Magnet is present or not.
              attachMagnetBoard();
              MagAttached = MagnetPresent; //Set to value of whatever the user sent.
              serialSend(command, MagAttached);

  } else if (command == "setupSignalMode"){
              ///Used to set up a dynamic signal mode. Note it will not do anything if systemEnable is true. We CAN do this why system is enabled, but not while a signal is active.
              //Argument 1 = signal index, argument 2 = signal repeat, argument 3 = signal mode, argument 4 = signal options, 
              int sIndex = argGetInt(commandString,0); // which signal
              bool sRepeat = argGetBool(commandString,1); // If it should repeat (True) or not (false)
              int sMode = argGetInt(commandString,2); // Mode, 0= ADC, 1 = DAC, 2 = GPIO
              int sOptions = argGetInt(commandString,3); // Options for the signal. For ADC this is the channel, for DAC this is the pin, for GPIO this is something else.
              bool isSlave = argGetBool(commandString, 4); // If this signal is a slave to another signal. This means it will only run if the master signal is active.
              if (sIndex<0 || sIndex>=numSignals){ // If the signal index is out of range then we have a problem.
                raiseError("Signal index out of range");
                return;
              }
              if (signalActive[sIndex] == true){
                raiseError("Cant setup signal " + String(sIndex) + " while it is active");
                return;
              }
              if (sMode>=SIGNALMODES || sMode<0){ // If the signal mode is out of range then we have a problem.
                raiseError("Signal mode out of range");
                return;
              }
              // Now we can set stuff
              signalRepeat[sIndex] = sRepeat;
              signalMode[sIndex] = sMode;
              signalIsSlave[sIndex] = isSlave;
              if (sMode == SIGNALMODE::DO) {
                if (sOptions == 1)
                  signalOptions[sIndex] = D_OUT_1;
                else if (sOptions == 2)
                  signalOptions[sIndex] = D_OUT_2;
                else if (sOptions == 3)
                  signalOptions[sIndex] = D_OUT_3;
                else if (sOptions == 4)
                  signalOptions[sIndex] = D_OUT_4;
                else {
                  raiseError("Invalid DO pin");
                  return;
                }
              } else if (sMode == SIGNALMODE::GPIO_WRITE) {
                // In this case assume that sOptions represents the GPIO _name_ i.e. 25 for GPIO25
                int gpioID = GPIOPinMap2(sOptions);
                int gpioPin = GPIOPinMap3(gpioID);

                GPIOInput[gpioID] = false;  // Set the GPIO output
                GPIOFunction[gpioID] = 0;   // Set the GPIO function to digital
                GPIOEnabled[gpioID] = true; // Set the GPIO enabled

                signalOptions[sIndex] = gpioPin;
              } else {
                signalOptions[sIndex] = sOptions;
              }
              serialSend(command, 1);
        
  } else if (command == "setupSignalOutput"){
              //Used to set up the data in a DAC output signal to generate AWG. This could also be used for GPIO output signals. This will be a series of timings and values. We can do this if system is enabled but not while signal is active.
              //Note incoming data should be signalID/numvalues/value/timing/value/timing with the number of values an timing dependent on numvalues.
              int sIndex = argGetInt(commandString,0); // which signal
              int sNumVals = argGetInt(commandString,1); // How many values we are receiving.
              if (sIndex<0 || sIndex>=numSignals){ // If the signal index is out of range then we have a problem.
                raiseError("Signal index out of range");
                return;
              }
              if (signalActive[sIndex] == true){
                raiseError("Cant setup signal " + String(sIndex) + " while it is active");
                return;
              }
              if (sNumVals>signalMaxLength || sNumVals<0){ // If the number of values is out of range then we have a problem.
                raiseError("Number of values out of range permissable, you might need to adjust signalMaxLength in main.cpp");
                return;
              }
              if ((signalMode[sIndex]!=SIGNALMODE::DAC) && 
                  (signalMode[sIndex]!=SIGNALMODE::MAGDAC) && 
                  (signalMode[sIndex]!=SIGNALMODE::MAGHALL_WRITE) &&
                  (signalMode[sIndex]!=SIGNALMODE::MAGCURR_WRITE) &&
                  (signalMode[sIndex]!=SIGNALMODE::DO) && 
                  (signalMode[sIndex]!=SIGNALMODE::LED) && 
                  (signalMode[sIndex]!=SIGNALMODE::LED_TIMED) &&
                  (signalMode[sIndex]!=SIGNALMODE::DO_TIMED) &&
                  (signalMode[sIndex]!=SIGNALMODE::GPIO_WRITE)){ // If we arent in an output mode then we have a problem.
                raiseError("Signal mode is not output");
                return;
              }
              //Now we get the number of value and timing pairs we expect based on sNumVals
              for (int i=0; i<sNumVals; i++){
                float sValue = argGetFloat(commandString,2+i*2); // Get the value
                signalData[sIndex][i] = sValue; // Set the value
                
                // Timing doesn't matter if it's a slave signal
                if (signalIsSlave[sIndex]) {
                  signalTiming[sIndex][i] = 0.0;  
                  continue;
                }
                float sTiming = argGetFloat(commandString,2+i*2 + 1); // Get the timing in miliseconds
                int sTimestep = int(sTiming*1000.0); // Convert to int and round to nearest microsceond.
                if (sTimestep<=0 && sTimestep!=-1){ // If the timing is out of range then we have a problem.
                  raiseError("That's a weird timing value you sent to setupSignalOutput friend. Your output setup will not be as you anticipated so fix it!");
                  return;
                } else if ((sTimestep<10) && (signalMode[sIndex]==SIGNALMODE::DAC)){
                  raiseError("The DAC can only update at ~100kHz so you need to wait at least 10us between updates. You might want to increase the timing. ");
                  // Note we still allow this but basically if they have gone faster than this it wont output the signal they asked for.
                  // Note also that the DAC output amplifiers have a limited slew rate of approximately 1V/us so if you are putting large signals at 100khz you may have some rounding of them...!
                }
                signalTiming[sIndex][i] = sTimestep; // Set the timing
              }
              // Finally we set any remaining values to -1 to indicate they are not used.
              for (int i=sNumVals; i<signalMaxLength; i++){
                signalData[sIndex][i] = 0.0;
                signalTiming[sIndex][i] = -1;
              }
              serialSend(command, 1);
  } else if (command == "setupSignalConductor") {
              int sIndex = argGetInt(commandString,0); // which signal
              int sNumVals = argGetInt(commandString,1); // How many values we are receiving.
              if (sIndex<0 || sIndex>=numSignals){ // If the signal index is out of range then we have a problem.
                raiseError("Signal index out of range");
                return;
              }
              if (signalActive[sIndex] == true){
                raiseError("Cant setup signal " + String(sIndex) + " while it is active");
                return;
              }
              if (sNumVals>signalMaxLength || sNumVals<0){ // If the number of values is out of range then we have a problem.
                raiseError("Number of values out of range permissable, you might need to adjust signalMaxLength in main.cpp");
                return;
              }

              for (int i=0; i<sNumVals; i++){
                signalData[sIndex][i] = 0.0; // Set all the data to zero. This could be used for ADC measurements. (Not tested!)

                float sTiming = argGetFloat(commandString,2+i); // Get the timing in miliseconds
                int sTimestep = int(sTiming*1000.0); // Convert to int and round to nearest microsceond.
                if (sTimestep<=0 && sTimestep!=-1){ // If the timing is out of range then we have a problem.
                  raiseError("That's a weird timing value you sent to setupSignalDAC friend. Your DAC setup will not be as you anticipated so fix it!");
                  return;
                } else if (sTimestep<10){
                  raiseError("The DAC can only update at ~100kHz so you need to wait at least 10us between updates. You might want to increase the timing. ");
                  // Note we still allow this but basically if they have gone faster than this it wont output the signal they asked for.
                  // Note also that the DAC output amplifiers have a limited slew rate of approximately 1V/us so if you are putting large signals at 100khz you may have some rounding of them...!
                }
                signalTiming[sIndex][i] = sTimestep; // Set the timing
              }
              // Finally we set any remaining values to -1 to indicate they are not used.
              for (int i=sNumVals; i<signalMaxLength; i++){
                signalData[sIndex][i] = 0.0;
                signalTiming[sIndex][i] = -1;
              }
              serialSend(command, 1);


  } else if (command == "setupSignalADC"){
              //Used to set up ADC to read a series of values. We can do this if system is enabled but not while signal is active.
              //Note incoming data should be signalID/numvalues/timestep with the number of values an timing dependent on numvalues.
              //todo: This could be extended to read out signals at variable timestep if you wish, following something like setupSignalDAC
              int sIndex = argGetInt(commandString,0); // which signal
              int sNumVals = argGetInt(commandString,1); // How many values we are receiving.
              float sTiming = argGetFloat(commandString,2); // Timestep between readings in milliseconds
              //Convert sTiming float to int
              int sTimestep = int(sTiming*1000.0); // Convert and round to nearest microsceond.
              if (sIndex<0 || sIndex>=numSignals){ // If the signal index is out of range then we have a problem.
                raiseError("Signal index out of range");
                return;
              }
              if (signalActive[sIndex] == true){
                raiseError("Cant setup signal " + String(sIndex) + " while it is active");
                return;
              }
              if (sNumVals>signalMaxLength || sNumVals<0){ // If the number of values is out of range then we have a problem.
                raiseError("Number of values you asked DAC to record is out is out of range");
                return;
              }
              if ((signalMode[sIndex]!=SIGNALMODE::ADC) && (signalMode[sIndex]!=SIGNALMODE::MAGADC) && (signalMode[sIndex]!=SIGNALMODE::MAGHALL_READ)){ // If we arent in ADC mode then we have a problem.
                raiseError("Signal mode is not ADC");
                return;
              }
              if (sTimestep<1000){ //Somehwat arbitrary timeout. Minimum you can tolerate is 0.6ms to read out one channel of the ADC.
                raiseError("ADC timestep is getting quite small given the fact it takes approx 0.6ms to read out one channel of the ADC. You therefore need to wait at least this time, or you can increase I2C bus speed at expense of potential errors. Note if you have multiple ADC readings running at this fast speed it will be even worse as they'll be waiting for one another to be done!.");
              }
              // Now we need to reset the signal data to zeros and all the relevant timing buffers to sTiming
              for (int i=0; i<sNumVals; i++){
                signalData[sIndex][i] = 0.0;
                signalTiming[sIndex][i] = sTimestep;
              }
              // Any entries after this need to have -1 timestep indicating they are off.
              for (int i=sNumVals; i<signalMaxLength; i++){
                signalData[sIndex][i] = 0.0;
                signalTiming[sIndex][i] = -1;
              }
              serialSend(command, 1);

  } else if (command == "startSignal"){
              // Used to start reading/sending a given signal. We can do this if system is enabled but not while signal is active.
              
              // incoming command should be just /signalIndex.
              int sIndex = argGetInt(commandString,0); // which signal
              if (sIndex<0 || sIndex>=numSignals){ // If the signal index is out of range then we have a problem.
                raiseError("Signal index out of range");
                return;
              }
              if (signalActive[sIndex] == true){
                raiseError("Cant start signal " + String(sIndex) + " while it is already active");
                return;
              }
              if (!systemEnable){ // If the system is not enabled then we have a problem.
                raiseError("System is not enabled so cant start that signal");
                return;
              }
              signalResetAndStart(sIndex); // Reset the signal and start it.
              serialSend(command, 1);
  
  } else if (command == "stopSignal"){
              // Used to stop an ongoing signal. Usually this wouldnt be run for ones not set up to repeat as they should just end, but what the hell.
              // incoming command should be just /signalIndex.
              int sIndex = argGetInt(commandString,0); // which signal
              if (sIndex<0 || sIndex>=numSignals){ // If the signal index is out of range then we have a problem.
                raiseError("Signal index out of range");
                return;
              }
              if (signalActive[sIndex] == false){
                raiseError("Signal " + String(sIndex) + " is already stopped. But its OK to ask twice :) ");
                return;
              }
              signalStopAndBackToStart(sIndex); // Reset the signal and stop it.
              serialSend(command, 1);

  } else if (command == "getSignalADC"){
              // Used to get the recorded ADC data back from the system.
              // Incoming command should just be signalIndex.
              // Note you CAN call this while the signal is still active (and repeating) which will just give you back the current value of the array.
              // Note that compared to the time signalResetAndStart was called, this data will be delayed by up to 600 us which is the time taken to read out the ADC (approx 591 us at 100khz clock rate)
              int sIndex = argGetInt(commandString,0); // which signal
              if (sIndex<0 || sIndex>=numSignals){ // If the signal index is out of range then we have a problem.
                raiseError("Signal index out of range");
                return;
              }
              if ((signalMode[sIndex] != SIGNALMODE::ADC) && (signalMode[sIndex] != SIGNALMODE::MAGADC) && (signalMode[sIndex] != SIGNALMODE::MAGHALL_READ)){ // If we arent in ADC mode then we have a problem.
                raiseError("Signal mode is not ADC");
                return;
              }
              // Now we send back the data
              serialSendData("SignalADC", signalData[sIndex], signalMaxLength); // Send the data back over serial

  } else if (command == "writeGPIO"){
              //Used to set the value of the GPIO pins. Note it will not do anything if systemEnable is false.
              // Argument 0 is pin number, argument 1 is a float with varying meanings depending on the current pinmode
              // start timer
          
              int arg0 = argGetInt(commandString,0); // which pin
              arg0 = GPIOPinMap2(arg0); // Map the pin numbers from GPIO names to 0-8 if needed.
              if (systemEnable == false || GPIOEnabled[arg0] == false){ // If stuff is off you shouldnt be messing with this buddy
                raiseError("Cant trigger a change in GPIO output while system disabled and/or that GPIO disabled");
                return;
              }
              if (GPIOInput[arg0] == true){ // If it is an input then we cant change it. Note this should work since any time you set and then do enable it should be in right state (maybe!)
                raiseError("Cant write GPIO (Software index " + String(arg0) +") while it is an input");
                return;
              }
              int thatGPIOFunction = GPIOFunction[arg0]; // Get the GPIO function
              float arg1 = argGetFloat(commandString,1); // Get the value we want to set it to

              int pin = GPIOPinMap3(arg0); // Map the pin number to the actual pin number on the device.
              if (thatGPIOFunction == 0){ // If it is a digital IO
                if (arg1 == 0.0){
                  digitalWriteFast(pin, LOW); //Philosophically this command migh tbe better in IOController but yolo.
                } else if (arg1 == 1.0){
                  digitalWriteFast(pin, HIGH);
                } else {
                  raiseError("Cant set GPIO to value other than 0 or 1");
                  return;
                }
                // Serial.println("Time was " + String(micros() - timer1) + " microseconds");  
              }
              serialSend(command, 1);

  } else if (command == "readGPIO"){
              // used to read GPIO pin if set that way.
              // Argument 0 is pin number
              //Really some of this stuff could go into IOController but yolo.
              int arg0 = argGetInt(commandString,0); // which pin
              arg0 = GPIOPinMap2(arg0); // Map the pin numbers from GPIO names to 0-8 if needed.
              if (systemEnable == false || GPIOEnabled[arg0] == false){ // If stuff is off you shouldnt be messing with this buddy
                raiseError("Cant read GPIO while system disabled and/or that GPIO disabled");
                return;
              }
              if (GPIOInput[arg0] == false){ // If it is an output then we shouldnt read it. Note this should work since any time you set and then do enable it should be in right state (maybe!)
                raiseError("Cant read GPIO (Software index " + String(arg0) +") while it is an output");
                return;
              }
              int thatGPIOFunction = GPIOFunction[arg0]; // Get the GPIO function
              

              int pin = GPIOPinMap3(arg0); // Map the pin number to the actual pin number on the device.
              if (thatGPIOFunction == 0){ // If it is a digital IO
                bool pinvalue = digitalReadFast(pin);
                serialSend("readGPIO", pinvalue); // Send the value back over serial
              } else if (thatGPIOFunction == 2){ // If it is an analog input
                // DO something here.
              }
  
  } else if (command == "setupGPIO"){
              //sets a specific GPIO pin state and mode. Note it will not do anything if systemEnable is true.
              // Argument 0 is pin number, argument 1 is whether it is enabled (true) or not (false), argument 2 is what function it is (0=GPIO, 1=PWM, 2=Analog Input), Argment 3 is whether it is an input (True) or output (false)
              if (systemEnable == true){ // If the system is disabled then we can't change the GPIO.
                raiseError("Cant change GPIO while system enabled");
                return;
              }
              int arg0 = argGetInt(commandString,0); // which pin
              
              bool arg1 = argGetBool(commandString,1); // If it is enabled (True) or not (false)
              int arg2 = argGetInt(commandString,2); // Mode, 0= Digital IO, 1 = PWM, 2 = Analog Input
              bool arg3 = argGetBool(commandString,3); // If it is an input (True) or output (false)
              arg0 = GPIOPinMap2(arg0); // Map the pin number to the actual pin number if needed
              GPIOInput[arg0] = arg3; // Set the GPIO input/output
              GPIOFunction[arg0] = arg2; // Set the GPIO function
              GPIOEnabled[arg0] = arg1; // Set the GPIO enabled/disabled
              Serial.println("Set up GPIO " + String(arg0) + " to enabled = " + String(arg1) + " function = " + String(arg2) + " input = " + String(arg3));
              
  } else if (command == "setSwitch"){
            //Used to switch the 16x switches on the syncboard. Note it can be done regardless of systemEnable state.
            // Argument 0 is switch number, argument 1 is the value from 1.0 (fully on) to 0.0 (off) and in between is PWM.
            //Note none of these states are stored in software, this means if you reset the system they will all default to off.
            int arg0 = argGetInt(commandString,0); // which switch
            float arg1 = argGetFloat(commandString,1); // Value to set it to.
            if (arg0<0 || arg0>16){ // If the switch number is out of range then we have a problem.
              raiseError("Switch number out of range");
              return;
            }
            if (arg1<0.0 || arg1>1.0){ // If the value is out of range then we have a problem.
              raiseError("Switch value out of range");
              return;
            }
            setSwitch(arg0, arg1); // Set the switch to the value requested.
            serialSend(command, 1);

  } else if (command == "writeDO"){
            // Used to switch one of the four digital outputs to a new state.
            //argument 0 is which output from 1-4
            // argument 1 is 1 or 0 for high or low.
            int arg0 = argGetInt(commandString,0); // which output
            bool arg1 = argGetBool(commandString,1); // Value to set it to.
            if (arg0<1 || arg0>4){ // If the output number is out of range then we have a problem.
              raiseError("Digital output number out of range");
              return;
            }
            if (arg0 == 1){
              digitalWriteFast(D_OUT_1, arg1);
            } else if (arg0 == 2){
              digitalWriteFast(D_OUT_2, arg1);
            } else if (arg0 == 3){
              digitalWriteFast(D_OUT_3, arg1);
            } else if (arg0 == 4){
              digitalWriteFast(D_OUT_4, arg1);
            } 
            serialSend(command, 1);

  } else if (command == "readDI"){
            // Used to read one of the four digital inputs.
            //argument 0 is which input from 1-4
            int arg0 = argGetInt(commandString,0); // which input
            if (arg0<1 || arg0>4){ // If the input number is out of range then we have a problem.
              raiseError("Digital input number out of range");
              return;
            }
            bool value = false;
            if (arg0 == 1){
              value = digitalReadFast(D_IN_1);
            } else if (arg0 == 2){
              value = digitalReadFast(D_IN_2);
            } else if (arg0 == 3){
              value = digitalReadFast(D_IN_3);
            } else if (arg0 == 4){
              value = digitalReadFast(D_IN_4);
            }
            serialSend("readDI", value); // Send the value back over serial.

  } else if (command == "startImageSequence"){ // 
        // Argument 1 is number of images to take, which is just used as a sanity check against your LED setup.
        if (systemExposing){ // If we are already exposing an image then we can't start a new one.
          raiseError("Cant start new image while exposing");
          return;
        } else if (systemEnable == false){ // If the system is disabled then we can't start a new image.
          raiseError("Cant start new image while system disabled");
          return;
        } else if (syncMode != 1){
          raiseError("Cant start new image manually via serial while in sync mode 0 or 2");
          return;
        }
        int arg0 = argGetInt(commandString,0); // First argument is how many images we are intending to take. We do some sanity checks on this first
        int firstZeroIndex = -1;
        bool nonZeroAfterFirstZero = false;
        for (int i = 0; i<maxImages; i++){ // Loop through all images and set them to inactive
          if(modeImageActive[i] == false && firstZeroIndex == -1){ // If we find a zero and we havent found one before then record it
            firstZeroIndex = i;
          } else if (modeImageActive[i] == true && firstZeroIndex != -1){ // If we find a non zero after the first zero then we have a problem
            nonZeroAfterFirstZero = true;
          }
        }
        if (firstZeroIndex != arg0){ // If we have a problem with the number of images then we have a problem
          raiseError("Cant start new image sequence, problem with number of images");
          Serial.println("firstZeroIndex = " + String(firstZeroIndex) + " arg0 = " + String(arg0));
          return;
        } else if (nonZeroAfterFirstZero){ // If we have a problem with the number of images then we have a problem
          raiseError("Cant start new image sequence, you have an image active after a non active image");
          return;
        } else if (arg0 == 0){ // If we have a problem with the number of images then we have a problem
          raiseError("Cant start new image sequence, you have no active images");
          return;
        }
        imageSequence(true); // Call the function that starts capturing an image. 1st flag means we are starting sequence.
        serialSend(command, 1);



  } else if (command == "setSyncMode") {
          //First argument = sync mode. 0 = system is not syncing with external devces, 1=PC drives syncBoard, 2=PC drives device drives syncBoard
          //Second argument = exposure mode. True = exposure time is set by camera trigger, false = exposure time is set by timer on syncboard.
          if (sequenceRunning){ // If we are currently imaging something then we can't change the sequence.
            raiseError("Cant change sync setup while imaging");
            return;
          }
          int arg0 = argGetInt(commandString,0); // Get the first argument from the serial input 
          if (arg0 < 0 || arg0 > 2){ 
            raiseError("Cant set sync mode, mode not recognised");
            return;
          }
          syncMode = arg0; // Set the sync mode
          bool arg1 = argGetBool(commandString,1); 
          modeLEDSwitchedByCamera = arg1; // Set the exposure mode
          serialSend(command, 1);
      

  } else if (command == "setupImageSequence"){ 
          //Setting up the image sequence. 
          // First we check if we are CURRENTLY imaging something
          if (sequenceRunning){ // If we are currently imaging something then we can't change the sequence.
            raiseError("Cant change imaging setup while imaging");
            return;
          }

          // First decompose the command. Note we are assuming the sender has sent the correct number of arguments here.
          for (int i = 0; i < maxImages; i++) {
            modeImageActive[i] = argGetBool(commandString, i*6);
            modeLEDChoice[i] = argGetInt(commandString, i*6 + 1);
            modeExposureTime[i] = argGetInt(commandString, i*6 + 2)*1000; //Note we convert from miliseconds inbound to microsceconds here.
            modeFilterWheelChange[i] = argGetBool(commandString, i*6 + 3);
            modeDMDChange[i] = argGetBool(commandString, i*6 + 4);
            modeOtherChange[i] = argGetBool(commandString, i*6 + 5);
          }
          Serial.println("We have the following values in modeImageActive: " + String(modeImageActive[0]) + " " + String(modeImageActive[1]) + " " + String(modeImageActive[2]) + " " + String(modeImageActive[3]));
          Serial.println(" We have the following values in modeExposureTIme " + String(modeExposureTime[0]) + " " + String(modeExposureTime[1]) + " " + String(modeExposureTime[2]) + " " + String(modeExposureTime[3]));
          // Now we check if we have any problems with the input. First if there is a case where imageActive goes false then true afterward.
          int firstZeroIndex = -1;
          bool nonZeroAfterFirstZero = false;
          for (int i = 0; i<maxImages; i++){ // Loop through all images and set them to inactive
            if(modeImageActive[i] == false && firstZeroIndex == -1){ // If we find a zero and we havent found one before then record it
              firstZeroIndex = i;
            } else if (modeImageActive[i] == true && firstZeroIndex != -1){ // If we find a non zero after the first zero then we have a problem
              nonZeroAfterFirstZero = true;
            }
          }
          if (nonZeroAfterFirstZero){ //if we fail this we set everything to false
            raiseError("Cant set up image sequence, you have an image active after a non active image"); 
            for (int i = 0; i < maxImages; i++) { modeImageActive[i] = false;  } // Reset everything to false.
            return;
          } 
          // Check if the person set some exposure times but it is going to ignore them.
          bool exposureTimeSetButIgnored = false;
          for (int i = 0; i<maxImages; i++){ // Loop through all images and set them to inactive
            if ( modeExposureTime[i] != 0){ // If we find a zero and we havent found one before then record it
              exposureTimeSetButIgnored = true;
            }
          }
          if (exposureTimeSetButIgnored && modeLEDSwitchedByCamera){ //if we fail this we set everything to false
            raiseError("You tried to set up an image sequence with some fixed exposure times but they are going to be ignored because exposure is set by camera trigger. Set exposure times to zero to avoid confusion."); 
            for (int i = 0; i < maxImages; i++) {modeImageActive[i] = false; } // Reset everything to false.
            return;
          }

          //Next we check if my LED choices are in bounds between 1 and 8
          for (int i = 0; i<maxImages; i++){ // Loop through all images and set them to inactive
            if(modeLEDChoice[i] < 0 || modeLEDChoice[i] > 8){ // If we find a zero and we havent found one before then record it
              raiseError("Cant set up image sequence, LED choice out of bounds");
              for (int i = 0; i < maxImages; i++) { modeLEDChoice[i] = 0; modeImageActive[i] = false; } //Set stuff to false.
              return;
            }
          }
          //Next we check if the exposure time is in bounds between 0 and 2000000 microseconds (2 seconds)
          for (int i = 0; i<maxImages; i++){ // Loop through all images and set them to inactive
            if(modeExposureTime[i] < 0 || modeExposureTime[i] > 2000000){ // If we find a zero and we havent found one before then record it
              for (int i = 0; i < maxImages; i++) { modeExposureTime[i] = 0; modeImageActive[i] = false; } //Set stuff to false.
              raiseError("Cant set up image sequence, exposure time out of bounds");
              return;
            }
          }


  } else if (command == "readADC"){
          //used to read ADC pin if set that way.
          // Argument 0 is channel number from 1 to 8
          int arg0  = argGetInt(commandString,0); // which pin
          int adcID = argGetInt(commandString,1); // which ADC ID
          if (systemEnable == false){ // If stuff is off you shouldnt be messing with this buddy
            raiseError("Cant read ADC while system disabled");
            return;
          }
          float adcValue = readADC(arg0, adcID); // Read the ADC value
          serialSend("readADC", adcValue); // Send the value back over serial

  } else if (command == "setDAC"){
          // used to set DAC on SyncBoard 
          // Argument 0 is channel number from 0 to 8, note that 0 corresponds to setting all channels at once.
          // Argument 1 is the value to set it to, should be a float form 0 to 3.3V.
          int channel = argGetInt(commandString,0); // which channel
          float value = argGetFloat(commandString,1); // value to set it to
          if (systemEnable == false){ // If stuff is off you shouldnt be messing with this buddy
            raiseError("Cant set DAC while system disabled");
            return;
          }
          setDAC(channel, value); // Set the DAC value
          serialSend(command, 1);

  } else if (command == "calibrateLED"){
          // Used to run the LED calibration on a specific LED.
          // Argument 0 is the LED number from 1 to 8
          // Argument 1 is the maximum current setting for that LED in Amps
          int arg0 = argGetInt(commandString,0); // which LED
          float arg1 = argGetFloat(commandString,1); // maximum current that will be allowed
          //Check if arg1 is NAN
          if (isnan(arg1)){
            raiseError("You needed to specify a LED current");
            return;
          }
          if (systemEnable == false  || LEDAttached == false){ // If stuff is off you shouldnt be messing with this buddy
            raiseError("Cant calibrate LED while system disabled or if LED board isnt there!");
            return;
          }
          if (arg0<1 || arg0>8){ // If the LED number is out of range then we have a problem.
            raiseError("LED number out of range");
            return;
          }
          if (arg1>0.0 && arg1<=15.0){ // If the value is out of range then we have a problem.
            calibrateLED(arg0,arg1); // Run the LED calibration
          } else {  
            raiseError("LED current out of range");
          }
          serialSend(command, 1);

  } else if (command == "setupLED")  {
          serialSend("Received setup LED command", 1);
          // Command to set up a LED mode ahead of it being used to do anything in particular. THis sets the feedback mode (current or optical) as well as the power level. Note it should already be calibrated!
          // Argument 0 is the LED number from 1 to 8
          // Argument 1 is the feedback mode 0 for current 1 for optical power.
          // Argument 2 is the LED set-point level which is between 0 and 1 (maximum) and should be approximately linear.
          int arg0 = argGetInt(commandString,0); // which LED
          int arg1 = argGetInt(commandString,1); // feedback mode
          float arg2 = argGetFloat(commandString,2); // LED set-point level
          if (systemEnable == false  || LEDAttached == false){ // If stuff is off you shouldnt be messing with this buddy
            raiseError("Cant setup LED while system disabled or if LED board isnt there!");
            return;
          }
          //check the channel choice is in range
          if (arg0<1 || arg0>8){ // If the LED number is out of range then we have a problem.
            raiseError("LED number out of range");
            return;
          }
          if (arg2<0.0005){
            raiseError("Note you are setting a really low LED power level. It will struggle to reliably produce this every time. You may need to rethink, activate it for a smaller time period, or do more regular calibration");
          }
          // Now set LED feedback mode and level
          if (arg1 == 0){ // If current feedback mode
            setLEDLevel(arg0, arg2, true); // Set the feedback mode as current feedback
          } else if (arg1 == 1) {
            setLEDLevel(arg0, arg2, false); // Set the feedback mode as optical power feedback
          } else {
            raiseError("Feedback mode not recognised");
            return;
          }
          serialSend(command, arg0);

  } else if (command == "getLEDSetup"){ //Function basically used to read back calibration values of a LED if you want to know what the current level corresponds to power-wise.
          //Worth noting the accuracy of this will depend on what one you are using for feedback. That is, if you have current feedback opticla power will be less accurace since they arent directly linearly proportional (and vice versa)
          //Argument 0 is which LEd you want details for.
          int arg0 = argGetInt(commandString,0); // which LED
          float result[4]; // Array to store the results. 1st arg is what is the current level set. 2nd arg is what current does this correspond to. 3rd arg is what optical power (in mV) does it correspond to. 4th arg is what is current max current allowed.
          getLEDSetup(arg0, result); // Get the LED setup
          serialSendData("getLEDSetup", result, 4); // Send the results back over serial

  } else if (command == "switchLEDTimed"){
          // Command used to turn a LED on for a certain amount of time. Note this requires you to set a time for how long you want it on.
          // Argument 0 is the LED number from 1 to 8
          // Argument 1 is the time in milliseconds you want it on for.
          int arg0 = argGetInt(commandString,0); // which LED
          float arg1 = argGetFloat(commandString,1); // time in milliseconds

          if (systemEnable == false  || LEDAttached == false){ // If stuff is off you shouldnt be messing with this buddy
            raiseError("Cant switch LED while system disabled or if LED board isnt there!");
            return;
          }
          if (arg0<1 || arg0>8){ // If the LED number is out of range then we have a problem.
            raiseError("LED number out of range");
            return;
          } 
          if (isnan(arg1) || arg1<0.0){ // If the time is out of range then we have a problem.
            raiseError("Time out of range or not provided to switchLEDTimed");
            return;
          }
          switchLEDTimed(arg0, arg1, true); // Switch the LED - note we are always telling it on in this case (third flag). We could also make this allow a fast-off mode if we wanted and this is implemented in LEDDriver but needs careful handling to avoid messing up NumberLEDsBeingTimed
          serialSend(command, arg0);

  } else if (command == "switchLED"){
          // Command used to just switch a LED on and leave it on.
          // Argument 0 is the LED number from 1 to 8
          // Argument 1 is bool whereh on (1) or off (0)
          int arg0 = argGetInt(commandString,0); // which LED
          bool arg1 = argGetBool(commandString,1); // on or off
          if (systemEnable == false  || LEDAttached == false){ // If stuff is off you shouldnt be messing with this buddy
            raiseError("Cant switch LED while system disabled or if LED board isnt there!");
            return;
          }
          if (arg0<1 || arg0>8){ // If the LED number is out of range then we have a problem.
            raiseError("LED number out of range");
            return;
          }
          switchLEDDirect(arg0, arg1); // Switch the LED as instructed
          serialSend(command, arg0);

  } else if (command == "disableAllLEDs"){
          if (systemEnable == false  || LEDAttached == false){ // If stuff is off you shouldnt be messing with this buddy
            raiseError("Cant switch LED while system disabled or if LED board isnt there!");
            return;
          }
          for (int i=1; i<=8; i++){
            switchLEDDirect(i, false); // Switch the LED as instructed
          }
          serialSend(command, 1);

  } else if (command == "measureLED"){
          // Command used to measure the LED current and optical power output when it is on/in a given state.
          
          int arg0 = argGetInt(commandString,0); // which LED
          if (systemEnable == false  || LEDAttached == false){ // If stuff is off you shouldnt be messing with this buddy
            raiseError("Cant measure LED while system disabled or if LED board isnt there!");
            return;
          }
          float result[2]; // Array to store the results  
          measureLED(arg0,result); // Measure the LED this is implemented in LEDDriver
          serialSendData("measureLED", result, 2); // Send the results back over serial
          //First argument will be current in Amps. Second argument will be optical power in mV.

  } else if (command == "measurePhotodiode"){
          //Command to read ont a photodiode even if there is no LED attached. Note that it will return APPROXIMAETLY 0 if there is no light but this is not calibrated.
          // Argument 0 is the photodiode number from 1 to 8
          int arg0 = argGetInt(commandString,0); // which photodiode
          if (systemEnable == false  || LEDAttached == false){ // If stuff is off you shouldnt be messing with this buddy
            raiseError("Cant measure photodiode while system disabled or if LED board isnt there!");
            return;
          }
          float result = measurePhotodiode(arg0); // Measure the photodiode
          serialSend("measurePhotodiode", result); // Send the result back over serial
  
  } else if (command == "systemEnable"){ // 
          enableSystem(true); // Enable the system. 

  } else if (command == "systemDisable"){ //
          enableSystem(false); // Disable the system including turning everything off.

  } else if (command == "setupFilterWheel"){
          // todo: Set up filter wheel if needed
          raiseError("Not implemented");
          return;

  } else if (command == "setupDMD"){
          // todo: Set up DMD if needed
          raiseError("Not implemented");
          return;

  } else if (command == "setupLEDFOrImaging"){ // Set up the LED for imaging i.e. tell the system which ones to turn on with each frame and what intensity.
          // todo: Set up LED if needed
          raiseError("Not implemented");
          return;

  } else if (command == "debug"){
    // digitalWrite(LED_1_Enable, HIGH);

    int arg0 = argGetInt(commandString,0); // Get the first argument from the serial input
    float arg1 = argGetFloat(commandString,1); // Get the first argument from the serial input
    Serial.println("Doing debug command");
    debugIO(arg0,arg1);


    //Trigger Test
    // digitalWrite(LED_1_Enable, !digitalRead(LED_1_Enable)); //dummy test trigger.
    // Serial.print("Got trigger and turned wire to " + String(digitalRead(LED_1_Enable)));
    
    
    // float arg0 = argGetFloat(commandString,0); // Get the first argument from the serial input
    // int arg1 = argGetInt(commandString,1); // Get the second argument from the serial input
    // // bool arg2 = argGetBool(commandString,2); // Get the third argument from the serial input
    // // float arg1 = argGetFloat(commandString,1); // Get the second argument from the serial input
    // // float arg2 = argGetFloat(commandString,2); // Get the third argument from the serial input
    // String arg2 = argGetString(commandString,2); // Get the third argument from the serial input  

    // float data[] = {13213213213131.5, -0.00000000000000531251112312316, 0.005312};
    // size_t arrayLength = sizeof(data) / sizeof(data[0]); // Calculate the length of the array
    // String command2 = "datalength + "  + String(arrayLength); // Replace with your actual command
    // // serialSendData(command2, data, arrayLength); // Call the function
    
    
    // serialSend("return data", arg0, arg1,arg2); // Send the first argument back over serial (with the word dog")
    
  } else if (command == "scanI2C") {
    I2CScan(); // Scan the I2C bus and print out the results.
  } 
  else if (command == "setupMagnetBoard") {
    setupMagnetBoard();
    serialSend("Setup magnet board complete", 1);
  } else if (command == "singleReadMagnetADC") {
    int channel = argGetInt(commandString,0); // Get the first argument from the serial input
    uint8_t value = singleReadMagnetADC(channel);
    serialSend("Read value ", value);
  } else if (command == "singleWriteMagnetDAC") {
    int channel = argGetInt(commandString,0); // Get the first argument from the serial input
    float value = argGetFloat(commandString,1);
    setMagDACI2C(channel, value);
  } else if (command == "calibrateMagnet") {
    serialSend("Calibrating magnet...", 1);
    calibrateMagnet();
    serialSend("Calibrated magnet", 1);
  } else if (command == "enableMagnet") {
    bool enable = argGetBool(commandString,0); // Get the first argument from the serial input
    enableMagnet(enable);
    serialSend("Enabled magnet ", enable);
  } else if (command == "setMagnet") {
    int NC = argGetInt(commandString, 0); 
    float value = argGetFloat(commandString, 1); 
    setMagnetCurrent(NC, value);
    serialSend("Set magnet current", value);
  } else if (command == "readHall") {
    int id = argGetInt(commandString, 0);
    float value = singleReadHall(id);
    serialSend("Read value ", value);
  } else if (command == "calibrateHall") {
    int id = argGetInt(commandString, 0);
    calibrateHall(id);
    serialSend("Calibrated Hall sensor " + String(id), 1);
  } else if (command == "setMagnetField") {
    int NC = argGetInt(commandString, 0);
    float value = argGetFloat(commandString, 1);
    setMagnetField(NC, value);
    serialSend("Set magnetic field", value);
  } else {
    raiseError("Command "+command+" not recognised");
  }
}

void setup() {

  
  
  //EEPROM.write(0,0); // write a value to the EEPROM
  // fwrite(0,0.0); // write a value to the EEPROM as a float
  initSignalBuffers(); // Put all the timed signals into a known "not started" state before anything can run them.
  serialSend("status", "setup complete"); // Send a status message over serial
  enableSystem(false); // Disable the system. This is the default state at startup.


  //todo: at this point all connections are enabled so you should actively turn the switches/DAC/ etc all offf.
}




void loop() {
  // First handle serial commands.
  // Note this might be delayed in activating as if we are doing some time sensitive task it can be delayed.
  // There are still potential issues with this for example if a serial command takes ages to execute, and you try to call it while some other time sensitive task is happening, it could delay things.
  
  if (Serial.available() > 0) { // Check if there is data available to read on serial and if system isnt busy with something time sensitive. Note I think the Serial.available() command takes <4us to run (below waht I can time with the chip)
    
    commandString = getSerial(); // Get the serial input
    commandString =serialDecode(commandString); // Decode the serial input. Note we are editing commandString so after this it shouldn't be possible to accidenttaly execute the same command again
    String command = getCommand(commandString); // Get the command out of the
    executeSerialCommand(command, commandString); // Execute the command

  }


  // Handle triggered behaviours
  if (systemEnable && syncMode!=0 ){ // If the system is enabled and we aren't in the "ignore triggers" syncmode=0, then we can do things with triggers and other jobs.
    // Check if any triggers have changed state, this is first thing to deal with
    for (int i = 0; i < numTriggers; i++) {
      int currentState = digitalReadFast(triggers[i]);
      if (currentState != prevTriggerState[i]) {
        // The pin has transitioned
        Serial.print("Got a trigger on channel " + String(i) + " and it is " + String(currentState) + " and previous was " + String(prevTriggerState[i]) + "\n  "); 
        triggerHandler(i, currentState); //Here current state will be the current (new) logic state of that triger signal.
      }
      prevTriggerState[i] = currentState;
      //todo: There may be an inssue in this loop in that in mode2 it will turn on the LED first before doing the rest of the stuff... e.g. since it doesnt reach the 5th trigger until after the LED one.
    }
    if (sequenceRunning){ //If we are in a sequence then we need to check if we need to do anything with it.
      imageSequence(); // This checks if we need to do anything with the ongoing image sequence. Note we could put this in an if statement conditional on !modeLEDSwitchedByCamera but this would prevent our timers for turning off LEDs if you use some of those.
    }
    // todo: Now look at our other tasks. This will involve various timers for instance to sync with SPI and so on. Need to have timer here for when exposure ends.
  }



  // Handle various timers
  if (systemEnable){
    // Handle heartbeat
    if (millis() - heartbeatTriggerTime > heartbeatTimeout){ // If we have reached the time required for triggering.
      digitalWriteFast(Heartbeat, !digitalReadFast(Heartbeat)); // Toggle the heartbeat pin.
      heartbeatTriggerTime = millis(); // Record the time we triggered the heartbeat.  
    }
    // Handle LEDs
    if (NumberLEDsBeingTimed > 0){ // If at least one of the LEDs is currently being timed we might have to do something to turn it off.
      LEDTimingHandler(); // Call the function that handles the LED timing which is inside LEDDriver. It handles how many LEDs are being timed.
    }
    // Handle DOs
    if (NumDOBeingTimed > 0){ // If at least one of the DOs is currently being timed we might have to do something to turn it off.
      DOTimingHandler(); // Call the function that handles the DO timing which is inside this file. It handles how many DOs are being timed.
    }

    // Handle other timers
    for (int i = 0; i < numSignals; i++) {
      //check if signal is active
      if (signalActive[i] == true && signalIsSlave[i] == false){
        //check if we have reached the time required for triggering.
        if (micros() - signalTriggerTime[i] > signalTimeout[i]){
          signalHandler(i); // Call the function that handles the signals.
        }
      }
    }

  } 


}