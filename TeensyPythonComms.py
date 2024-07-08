#%%
import serial
import time

#%%
ser = serial.Serial('/dev/syncboard', 2000000, timeout=0)

#%%
data = []

start_character="$"
end_character = "#"
transmission_stop = "%"
delimeter = "/"

def raiseError(message):
    # Custom error function, can change it later if you wish to actually raise an error rather than print it/add timestamps etc.
    print(message)

def disableSystem():
    command = "$systemDisable#%" #system can be on for this
    ser.write(command.encode())  # Send the command

def enableSystem():
    command = "$attachLED/true#%" #Tell it we want to set up to attach the LEDs
    ser.write(command.encode())  # Send the command
    command = "$systemEnable#%" #system can be on for this
    ser.write(command.encode())  # Send the command

def parse_reply(reply):
    # Check that the reply starts and ends with right characters
    reply = reply.strip() # Trim any whitespace.
    if not reply.startswith(start_character) or not reply.endswith("%"):
        raiseError("Invalid reply: " + reply)
        return "Error", None #Dont want to act in this case since something is corrupted.

    # Remove the transmission integrity characters.
    reply = reply[1:-2]
    
    # Split on the '/' delimiter
    parts = reply.split(delimeter)
    
    # Extract the first string command name.
    command = parts[0]
    
    # Convert the rest of the parts to numbers if they're in scientific notation
    data = []
    for part in parts[1:]:
        try:
            # Try to convert to a float
            number = float(part)
            data.append(number)
        except ValueError:
            # If it's not a number, keep it as a string
            data.append(part)
    
    return command, data



def formatImageSequence(lst, max_length):
    if lst is None:
        lst = [0] * max_length
    else:
        if isinstance(lst, int):
            lst = [lst]
        lst.extend([0] * (max_length - len(lst)))
    return lst

def setupImageSequence(ImageActive, LEDChoice, ExposureTime = None,FilterWheelChange = None, DMDChange = None,OtherChange = None):
    # Command used to set up the serial command for the image sequence.
    # imageActive: Array of length maxImages, 1 if image is active, 0 if not.
    # LEDChoice: Array of length maxImages, 1 for LED1, 0 for LED2, 
    # ExposureTime: Array of length maxImages, exposure time in microseconds. Must be an integer. 
    # FilterWheelChange: Array of length maxImages, 1 if filter wheel is changing, 0 if not.
    # DMDChange: Array of length maxImages, 1 if DMD is changing, 0 if not.
    # OtherChange: Array of length maxImages, 1 if other things are changing, 0 if not.
    maxImages = 4 # Max number of images that can be taken in a sequence, needs to be same in C++ and Python.

    # First we fix exposure time, the purpose of this is that if we are calculating these in floats it will convert to int here. Note that we don't lose any accuracy by rounding since it is in microsecodns and we cant time below 1us ANYWAY
    if ExposureTime is not None:
        if not isinstance(ExposureTime, list):
            ExposureTime = [round(ExposureTime)]
        elif isinstance(ExposureTime, list):
            ExposureTime = [round(i) for i in ExposureTime]

    imageActive = formatImageSequence(ImageActive, maxImages) # Make sure the array is the right length and entries etc
    LEDChoice = formatImageSequence(LEDChoice, maxImages)
    ExposureTime = formatImageSequence(ExposureTime, maxImages) # Exposure time is in milseconds.
    FilterWheelChange = formatImageSequence(FilterWheelChange, maxImages)
    DMDChange = formatImageSequence(DMDChange, maxImages)
    OtherChange = formatImageSequence(OtherChange, maxImages)


    command = start_character + "setupImageSequence"
    # Now loop through each array and construct the command
    for i in range(maxImages):
        command += delimeter + str(imageActive[i]) + delimeter + str(LEDChoice[i]) + delimeter + str(ExposureTime[i]) + delimeter + str(FilterWheelChange[i]) + delimeter + str(DMDChange[i]) + delimeter + str(OtherChange[i])

    command += end_character + transmission_stop
    return command



def getSerialResponses(time_responding):

    start_time = time.perf_counter()
    while time.perf_counter() - start_time < time_responding:
        if ser.in_waiting > 0:
            reply = ser.readline().decode()  # Read the reply
            print("Time received: {:.7f}".format(time.perf_counter()) + " Reply: " + reply)




def testtrigger():
    command = "$setSyncMode/1/0#%" 
    # 1st argument is what syncmode, 0 is not syncing , 1 is PC drives syncboard, 2 is PC drives external device which then drives trigger.
    # 2nd argument 1 means modeLEDSwitchedByCamera (i.e. the LED stays on until camera trigger tells it to turn off), 0 means LED is turned on immediately and turned off by the timers set in setupImageSequence.
    
    ser.write(command.encode())  # Send the command

    command = "$setupLED/8/1/0.1#%" # LED 8 set to current feedback mode (arg1=0 is current, arg1=1 would be optical feedback) and intensity 0.1
    ser.write(command.encode())  # Send the command


    command = setupImageSequence([1 ,1],[1,8],[1,100]) # First argument is array with which image indexes are active, should basically be 1's as long as the number ofi mages. 
    # second argument is which LED on the board is allocated to that slot in the image sequence
    # Third argument is exposure time in miliseconds.
    print("Command: " + command)    
    ser.write(command.encode())  # Send the command
    

    command = "$startImageSequence/2#%" # This starts the image sequence as defined above.  The 2 is the number of images in the sequence and is basically just a sanity check as it should already know this.
    # Note if you set modeLEDSwitchedByCamera to 1 in the setSyncMode but you havent set the camera up to send LED triggers itself, then starting the image sequence might not work since the LEDs wont be triggered at any point...
    ser.write(command.encode())  # Send the command

    # command = "$systemDisable#%" # This starts the image sequence as defined above.  The 2 is the number of images in the sequence and is basically just a sanity check as it should already know this.
    # # Note if you set modeLEDSwitchedByCamera to 1 in the setSyncMode but you havent set the camera up to send LED triggers itself, then starting the image sequence might not work since the LEDs wont be triggered at any point...
    # ser.write(command.encode())  # Send the command



    getSerialResponses(2)


def testSignaltiming():
    time_sent = time.perf_counter()
    print("Time sent: {:.12f}".format(time_sent))
    ser.write(command.encode())  # Send the command
    while ser.in_waiting == 0:  # Wait for a reply
        pass
    reply = ser.readline().decode()  # Read the reply
    #time_received = time.process_time()  # Record the time when the reply was received
    time_received = time.perf_counter()
    print("Time received: {:.12f}".format(time_received))
    time_ms=(1e3)*(time_received - time_sent)
    print("miliseconds roundtrip: " + str(time_ms))
    print("Reply: " + reply)
    command, data = parse_reply(reply)
    print("Command: " + command)
    print("Data: " + str(data)) 



def GPIOTest():
    command = "$systemDisable#%" #System must be disabled to change GPIO
    ser.write(command.encode())  # Send the command
    
    command = "$setupGPIO/13/1/0/0#%" #GPIO 13, enabled, mode 0 (digital IO), and output state.
    ser.write(command.encode())  # Send the command

    command = "$setupGPIO/3/1/0/1#%" #GPIO 3 (aka 27), enabled, mode 0, and input state.
    ser.write(command.encode())  # Send the command
    
    command = "$systemEnable#%"
    ser.write(command.encode())  # Send the command
   
    time.sleep(1)
    #Now send some values to the GPIO 13 which is output.
    time_sent = time.perf_counter()  # Record the time when the command was sent
    command = "$writeGPIO/13/0#%" # GPIO 13 set it to off.
    ser.write(command.encode())  # Send the command
    command = "$writeGPIO/13/1#%" # GPIO 13 set it to on.
    ser.write(command.encode())  # Send the command
    command = "$writeGPIO/13/0#%" # GPIO 13 set it to off.
    ser.write(command.encode())  # Send the command
    command = "$readADC/1#%" # GPIO 13 set it to on.
    ser.write(command.encode())  # Send the command

    #now print how much time it tooked for this fast trigger
    time_received = time.perf_counter()
    print("Time delay: {:.12f}".format(time_received-time_sent))
  
    getSerialResponses(2)

    
def DACSetSingleTest():
    #Demo of writing a single DAC value.  
    # command = "$systemEnable#%" # Enable the system
    # ser.write(command.encode())  # Send the command
    # time.sleep(0.5)

    command = "$setDAC/1/1.0#%" # set DAC channel 1 to 1.5 volts.
    ser.write(command.encode())  # Send the command
    getSerialResponses(0.2) # Wait for 0.2 seconds for the reply/s

def DACSetSequenceTest():
    #DEmo of writing a series of DAC values then getting it to repeat.
    command = "$systemEnable#%" #system can be on for this
    ser.write(command.encode())  # Send the command
    
    command = "$setupSignalMode/0/1/1/1#%" #0 = signal index / 1 = repeat / 1= DAC / 1 = channel 1
    ser.write(command.encode())  # Send the command

    # command = "$setupSignalDAC/0/4/0.1/2/1.0/3/0.5/2/0.3/3#%" #0=signal index 0 / 4 = number of voltage/timing pairs we are going to send. / 0.1 = first voltage / 2 = first time in ms / 1.0 second voltage / 3 = second time in ms .... and so on
    command = "$setupSignalDAC/0/4/0.1/0.01/3.1/0.01/0.1/0.01/3.1/0.01#%"  #This is to test fast speed 10us transition square wave,
    # Note for above the data (voltage) is in Volts and the timings are in miliseconds. A pair like 0.1/2/ would mean 0.1 volts for 2ms. 
    # You can run the timings down to about 100 us but below that you will get errors as it takes some nonzero time to set the SPI dac.
    # Note the final value of the signal is what will be left in the DAC when you stop the signal (or it ends if reepeat= false)
    ser.write(command.encode())  # Send the command

    command = "$startSignal/0#%" #0 = signal index 0
    ser.write(command.encode())  # Send the command

    time.sleep(3)
    command = "$stopSignal/0#%" #0 = signal index 0 (stop recording) Note this would do nothing if we have not had a repeating signal that has finished recording.
    ser.write(command.encode())  # Send the command

    getSerialResponses(5)
    
def ADCReadSingleTest():
    #Demo of reading a single ADC value.    
    command = "$systemEnable#%" # Enable the system
    ser.write(command.encode())  # Send the command
    time.sleep(1)

    command = "$readADC/1#%" # Read GPIO channel 1
    ser.write(command.encode())  # Send the command
    getSerialResponses(0.2) # Wait for 0.2 seconds for the reply/s


def ADCReadSequenceTest():
    #Demo of reading a series of ADC values from device, requires setup of signal sequence.
    command = "$systemEnable#%" #system can be on for this
    ser.write(command.encode())  # Send the command

    #First we need to set up the signal and tell it what kind of thing it will be dealing with.
    command = "$setupSignalMode/0/0/0/1#%" #0 = signal index / 0 = not repeat / 0 = ADC / 1 = channel 1
    # command = "$setupSignalMode/0/1/0/1#%" #0 = signal index / 1 =  repeat / 0 = ADC / 1 = channel 1
    ser.write(command.encode())  # Send the command

    #Next we set up that specific signal
    command = "$setupSignalADC/0/50/1#%" #0=signal index 0 / 50 = number values to record before stopping / 1 = time between samples in miliseconds
    ser.write(command.encode())  # Send the command

    #Now start recording the above setup
    command = "$startSignal/0#%" #0 = signal index 0
    ser.write(command.encode())  # Send the command

    time.sleep(0.5)
    command = "$stopSignal/0#%" #0 = signal index 0 (stop recording) Note this would do nothing if we have not had a repeating signal that has finished recording.
    ser.write(command.encode())  # Send the command


    # time.sleep(0.5) #Wait for a bit to make sure it has started recording.
    #Now get the recorded signal. Note if you dont have a delay above this it might return partially complete signal since it takes time to record.
    command = "$getSignalADC/0#%" #0 = signal index 0
    ser.write(command.encode())  # Send the command

    getSerialResponses(1)

def DACSetADCReadTest():
    # This is an example where we set up the DAC to write some signal sequence and read it back with the ADC.
    command = "$systemEnable#%" #system can be on for this
    ser.write(command.encode())  # Send the command

    command = "$setupSignalMode/0/0/1/1#%" #0 = signal index / 0 = no repeat / 1= DAC / 1 = channel 1
    ser.write(command.encode())  # Send the command

    command = "$setupSignalDAC/0/8/0.1/5/1.1/5/0.1/5/1.1/5/0.1/5/1.1/5/0.1/5/0.2/5/#%" #0=signal index 0 / 8 = number of voltage/timing pairs we are going to send.
    # Note for above the data (voltage) is in Volts and the timings are in miliseconds. A pair like 0.1/2/ would mean 0.1 volts for 2ms. 
    # You can run the timings down to about 100 us but below that you will get errors as it takes some nonzero time to set the SPI dac.
    ser.write(command.encode())  # Send the command

    #Set up the ADC
    command = "$setupSignalMode/1/0/0/1#%" #1 = signal index / 0 = not repeat / 0 = ADC / 1 = channel 1
    ser.write(command.encode())  # Send the command

    #Next we set up that specific signal
    command = "$setupSignalADC/1/75/1#%" #0=signal index 0 / 50 = number values to record before stopping / 1 = time between samples in miliseconds
    ser.write(command.encode())  # Send the command

    time.sleep(0.2)
    #Now start recording 
    command = "$startSignal/1#%" #0 = signal index 0
    ser.write(command.encode())  # Send the command
    time.sleep(0.01)
    #Now start writing the DAC signal
    command = "$startSignal/0#%" #0 = signal index 0
    ser.write(command.encode())  # Send the command

    time.sleep(0.2) # The full record should take 99*2 ~200ms so we wait for 0.2s to make sure it has finished.

    # command = "$stopSignal/0#%" #0 = signal index 0 (stop recording) Note this would do nothing if we have not had a repeating signal that has finished recording.
    # ser.write(command.encode())  # Send the command


    # time.sleep(0.5) #Wait for a bit to make sure it has started recording.
    #Now get the recorded signal. Note if you dont have a delay above this it might return partially complete signal since it takes time to record.
    command = "$getSignalADC/1#%" #1 = signal index 1
    ser.write(command.encode())  # Send the command

    getSerialResponses(2)

def testSwitches():
    command = "$systemEnable#%" #system can be on for this
    ser.write(command.encode())  # Send the command

    command = "$setSwitch/0/0.0#%" #Turns every switch off.
    ser.write(command.encode())  # Send the command
     
    command = "$setSwitch/1/1.0#%" #Turns switch 1 to fully on
    ser.write(command.encode())  # Send the command

    command = "$setSwitch/5/0.5#%" #Turns switch 5 to half intensity
    ser.write(command.encode())  # Send the command


def testDigitalIO():
    command = "$systemEnable#%" #system can be on for this
    ser.write(command.encode())  # Send the command

    command = "$writeDO/1/1#%" #Write 1 to first digital output.
    ser.write(command.encode())  # Send the command

    command = "$readDI/1#%" #Read 1st digital output
    ser.write(command.encode())  # Send the command

    command = "$writeDO/1/0#%" #Write 1 to first digital output.
    ser.write(command.encode())  # Send the command

    command = "$readDI/1#%" #Read 1st digital output
    ser.write(command.encode())  # Send the command

    getSerialResponses(1)

def LEDTest():
    # command = "$factoryReset#%" # DO a factory reset so we clear the EEPROM if needed for calibration.
    # ser.write(command.encode())  # Send the command
    LEDID=3
    # maxcurrent = 6.0
    # command = "$calibrateLED/" + str(LEDID) +"/" + str(maxcurrent) +"#%" # Calibrate led 8 up to maximum current of 10Amps. Note calibration is a weird command you should not run at same time as anything else since it messes with watchdog and main loop of prgoram
    # ser.write(command.encode())  # Send the command

    command = "$setupLED/" + str(LEDID) +"/0/0.29#%" # LED set to current feedback mode (arg1=0 is current, arg1=1 would be optical feedback) and intensity 0.1
    ser.write(command.encode())  # Send the command

    
    for i in range(1):
        command = "$switchLEDTimed/" + str(LEDID) +"/1.0#%" # Turn led 8 on for
        ser.write(command.encode())  # Send the command
        time.sleep(0.002)
    
    command = "$switchLEDTimed/" + str(LEDID) +"/300.0#%" # Turn led 8 on for
    ser.write(command.encode())  # Send the command
    # time.sleep(0.1)
    command = "$measureLED/" + str(LEDID) +"#%" # Tell it to measure the LED intensityl the values it returns will be the current (first argument, Amps) and optical power (second argument, mV) for theLED which should be currently on.
    ser.write(command.encode())  # Send the command
    time.sleep(0.1)

    command = "$getLEDSetup/" + str(LEDID) +"#%" # Retrieve current setup. 1st arguent is level set. 2nd argument is what current that corresponds to (Amps), 3rd arg is what optical power (mV), 4th argument is what is maximum current allowed for that device (Amps)
    ser.write(command.encode())  # Send the command

    
    command = "$switchLED/" + str(LEDID) +"/1#%" # Turn led on
    ser.write(command.encode())  # Send the command
    time.sleep(10)

    command = "$switchLED/" + str(LEDID) +"/0#%" # Turn led  off
    ser.write(command.encode())  # Send the command

    # command = "$switchLEDTimed/" + str(LEDID) +"/1.0#%" # Turn led 8 on for 1 milisecond
    # ser.write(command.encode())  # Send the command

    # command = "$measureLED/8#%" # Tell it to measure the current (first argument, Amps) and optical power (second argument, mV) for the 8th LED which should be currently on.
    # ser.write(command.encode())  # Send the command


    getSerialResponses(1)

def LEDCalibrate():
    #Demo function showing calibration of all LEDs. Note they should be plugged in as described in Document MH19.
    LEDID=1
    maxcurrent = 10.0
    command = "$calibrateLED/" + str(LEDID) +"/" + str(maxcurrent) +"#%" # Calibrate led 8 up to maximum current of maxcurrent.
    ser.write(command.encode())  # Send the command

    command = "$setupLED/" + str(LEDID) +"/1/0.05#%" # LED set to optical feedback mode (arg1=0 is current, arg1=1 would be optical feedback) and intensity 0.1
    ser.write(command.encode())  # Send the command

    LEDID=2
    maxcurrent = 8.0
    command = "$calibrateLED/" + str(LEDID) +"/" + str(maxcurrent) +"#%" # Calibrate led 8 up to maximum current of maxcurrent.
    ser.write(command.encode())  # Send the command

    command = "$setupLED/" + str(LEDID) +"/1/0.05#%" # LED set to optical feedback mode (arg1=0 is current, arg1=1 would be optical feedback) and intensity 0.1
    ser.write(command.encode())  # Send the command

    LEDID=3
    maxcurrent = 12.0
    command = "$calibrateLED/" + str(LEDID) +"/" + str(maxcurrent) +"#%" # Calibrate led 8 up to maximum current of maxcurrent.
    ser.write(command.encode())  # Send the command

    command = "$setupLED/" + str(LEDID) +"/1/0.05#%" # LED set to optical feedback mode (arg1=0 is current, arg1=1 would be optical feedback) and intensity 0.1
    ser.write(command.encode())  # Send the command

    LEDID=4
    maxcurrent = 8.0
    command = "$calibrateLED/" + str(LEDID) +"/" + str(maxcurrent) +"#%" # Calibrate led 8 up to maximum current of maxcurrent.
    ser.write(command.encode())  # Send the command

    command = "$setupLED/" + str(LEDID) +"/1/0.05#%" # LED set to optical feedback mode (arg1=0 is current, arg1=1 would be optical feedback) and intensity 0.1
    ser.write(command.encode())  # Send the command


    LEDID=7
    maxcurrent = 6.0
    command = "$calibrateLED/" + str(LEDID) +"/" + str(maxcurrent) +"#%" # Calibrate led 8 up to maximum current of maxcurrent.
    ser.write(command.encode())  # Send the command

    command = "$setupLED/" + str(LEDID) +"/1/0.05#%" # LED set to optical feedback mode (arg1=0 is current, arg1=1 would be optical feedback) and intensity 0.1
    ser.write(command.encode())  # Send the command

    getSerialResponses(5)


def LEDDisco():
    # Silly function to shine the non-UV LEDs one after another.
    intensity=0.1
    #First loop through first four leds and set them up
    LEDs = [1,2,3,4]
    for i in LEDs:
        # command = "$setupLED/" + str(i) +"/0/" + str(intensity) +"#%" #Current feedback mode
        command = "$setupLED/" + str(i) +"/1/" + str(intensity) +"#%" #Power feedback mode
        # if i==3
        #     command = "$setupLED/" + str(i) +"/0/" + str(0.1) +"#%"
        # if i==4:
        #     command = "$setupLED/" + str(i) +"/0/" + str(0.1) +"#%"
        ser.write(command.encode())  # Send the command
    
    timedelay  = 20 # time between LEDs in ms
    numloops = 10
   
    for i in range(numloops):
        for j in LEDs:
   
            command = "$switchLEDTimed/" + str(j) +"/" + str(timedelay) +"#%"

            ser.write(command.encode())  # Send the command
            # time.sleep(1*timedelay/1000)
        time.sleep(0.5*timedelay/1000)
    
       

def EnableTest():
    # This is a test to see how well the system is able to handle being enabled and disabled rapidly.

    command = "$setupLED/8/0/0.1#%" # LED 8 set to current feedback mode (arg1=0 is current, arg1=1 would be optical feedback) and intensity 0.1
    ser.write(command.encode())  # Send the command

    
    command = "$switchLED/8/1#%" # Turn led 8 on
    ser.write(command.encode())  # Send the command
    

    command = "$switchLEDTimed/8/200.0#%" # Turn led 8 on for 1 milisecond
    ser.write(command.encode())  # Send the command

    command = "$systemDisable#%" #system can be on for this
    ser.write(command.encode())  # Send the command
    time.sleep(0.1)
    command = "$systemDisable#%" #system can be on for this
    ser.write(command.encode())  # Send the command

    command = "$systemEnable#%" #system can be on for this
    ser.write(command.encode())  # Send the command
    time.sleep(0.1)
    command = "$setupLED/8/0/0.7#%" # LED 8 set to current feedback mode (arg1=0 is current, arg1=1 would be optical feedback) and intensity 0.1
    ser.write(command.encode())  # Send the command


    command = "$switchLEDTimed/8/200.0#%" # Turn led 8 on for 1 milisecond
    ser.write(command.encode())  # Send the command

    getSerialResponses(4)



def debugtest():
    # Just for debugging calls some random hacky command
    # command = "$systemEnable#%" #Turn it on if not already.
    # ser.write(command.encode())  # Send the command
      

    command = "$debug/8/0.9#%" # 1st param int 2nd param float.
    ser.write(command.encode())  # Send the command

    # command = "$debug/8/0.0#%" # 1st param int 2nd param float.
    # ser.write(command.encode())  # Send the command
    getSerialResponses(2.25) # Wait for 0.2 seconds for the reply/s




# enableSystem()
# time.sleep(0.5)
# ADCReadSingleTest()
# ADCReadSequenceTest()
# DACSetSingleTest()l
# DACSetSequenceTest()
# DACSetADCReadTest()
# testSwitches()
# testDigitalIO()
# LEDTest()
# LEDDisco()
# testtrigger()   
# EnableTest()
# debugtest()
# %%
