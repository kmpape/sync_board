#include "Arduino.h"
#include "GlobalVariables.h"

// This is a very high tech serial command standard.

// The idea is that we have a start character, end character and delimeter character.
String start_character="$";
String end_character = "#";
String transmission_stop = "%";
String delimeter = "/";
String error_command = "error";
int data_transmission_sigfigs = 7; //Note making this greater than 7 will not actually achieve anything with the teensy4.1 since it only has 7 sigfigs of precision in floats


bool serialReady = true; //Flag to say if we are free to communicate or not. Currently not used.
// A caution for this is that if we send commands from other areas while we are thinking we aren't ready, they wont get out! i.e. it all needs to be in main loop.

//Defining some objects to store incoming data temporarily.
const int max_serial_length_chars = 1000; // Define a constant for the maximum number of characters in the incoming data
char incomingCommand[max_serial_length_chars]; // Define a char array to store the incoming data
String receivedCommand; // Define a string to store the above data once converted


// Function declarations since they need to be used throughout.
void raiseError(String errorString);



/////////////////// ENCODING MESSAGES ///////////////////////
//The process is we get called from the send function above, which encodes the message then transmits it. The encoding is needing to be different if you are sending some arbitrary thing versus an array.

// Below is some GPT magic for dealing with encoding of arbitrary numbers of arguments and types. 

// This function is the last one called in the recursion and just adds the last argument to the string



String serialEncodeData(String command, float* array, size_t arrayLength) {
  // This function is meant to be used with arrays of data.
  String encodedCommand = start_character + command;
  for (size_t i=0; i<arrayLength; i++){
    encodedCommand += delimeter;
    char buffer[32]; 
    sprintf(buffer, "%.*e", data_transmission_sigfigs, array[i]); //Convert to string in scientific notation with some number of decimal places.
    encodedCommand += String(buffer);
    
  }
  encodedCommand += end_character;
  encodedCommand += transmission_stop; //This is to tell the PC that we are done sending data.
  return encodedCommand;
}


bool serialTransmit(String commandString, bool wait=true,bool iserror=false){
  // Function to transmit a command over serial - requres already encoded command.
  // If wait is true then we will wait until we are free to send before sending
  // iserror is there for future use if needed. 
  // Note this will return true/false depending on success.
  if (commandString.startsWith(start_character) && commandString.endsWith(transmission_stop)){
    // THe happy case where formatting was correct.
  } else {
    // Here we want to check if the string error_command appears anywhere in commandString.
    // This is because it can otherwise get into an infinte loop where the error command itself is creating errors.
    // Note all of this stuff will happen regardless of if we are waiting or not.
    if (commandString.indexOf(error_command) != -1){
      // In this case we have an error command so we should send it no matter what.
      Serial.println(start_character + error_command + delimeter + "You somehow made an error in your error command you silly goose" + end_character + transmission_stop);
      return false;
    } 
    raiseError("Command created for transmission was not valid");
    return false;
  }
  
  if (wait){ //If we are meant to wait
    if(!serialReady){ //And if we are then ready - note this currently does nothing since we dont use serialReady! 
      return false;
    }
  }

  Serial.println(commandString);

  return true; // We have succeeeded in sending command we think!

} 

// Specialization helper for float below.
String toString(float arg) {
    char buffer[32];
    sprintf(buffer, "%.*e", data_transmission_sigfigs, arg);
    return String(buffer);
}





/// STuff that needs to get coppied over to .h if you edit it////



// Helper function for converting to string
template<typename T>
String toString(T arg) {
    return String(arg);//Note this will call the toString function above - we need this to handle floats into scientific notation.
}



template<typename T> 
void serialEncodeHelper(String& command, T&& arg) {
  command += delimeter;
  command += toString(arg); //Note this will call the toString function above - we need this to handle floats into scientific notation.
}




// THis one takes arbitrary inputs and adds them onto the running string.
template<typename T, typename... Args>
void serialEncodeHelper(String& command, T&& arg, Args&&... args) {
  command += delimeter;
  command += toString(arg);  
  serialEncodeHelper(command, std::forward<Args>(args)...);
}

template<typename... Args>
String serialEncodeCmd(String command, Args&&... args) {
  // This isnt meant to be used with arrays, just commands.
  String encodedCommand = start_character + command;
  serialEncodeHelper(encodedCommand, std::forward<Args>(args)...);
  encodedCommand += end_character; //This is to signify end of arguments.
  encodedCommand += transmission_stop; //This is to tell the PC that we are done sending data.
  return encodedCommand;
}

template<typename... Args>
bool serialSend(const String& command, Args&&... args) {
  // This is the main send command which is really the only send command that should be used outside this function.
  // Note this wont work if your argument includes an array.
  String encodedCommand = serialEncodeCmd(command, std::forward<Args>(args)...); //Send it
  return serialTransmit(encodedCommand); //Note this will return true/false depending on success.
}

/// End STuff that needs to get coppied over to .h if you edit it////



bool serialSendData(const String& command, float* array, size_t arrayLength) {
  // Special case if we are sending an array of numbers. Note it can only send one array at a time and you need to specify length!
  String encodedData = serialEncodeData(command, array, arrayLength); //Encode it
  return serialTransmit(encodedData);
}


////////////////// DECODING MESSAGES ///////////////////////
//Summary: We have a string like $command/arg0/arg1/arg2/arg3/arg4/arg5/arg6/arg7/arg8/arg9/arg10/arg11/arg12/arg13/arg14/arg15/arg16/arg17/arg18/arg19/arg20#
// We have a serialDecode which takes the start/end characters off and gives back the command/arg0/arg1/arg2... structure.
// We have aGet or argumentGet which allows you to extract an argument at some position along that string. 


String serialDecode(String commandRaw){
  // Function that takes in serial commands from the PC and decodes them.
  // Decoding very simple for now, literally just take off the start/end characters.

  commandRaw = commandRaw.trim(); // Remove any whitespace
  
  //First we check that it starts/ends wth right characters
  if (commandRaw.startsWith(start_character) && commandRaw.endsWith(end_character)){
    //In this case the string was fine so we can continue
  } else {
    raiseError("Received serial command was not valid (e.g. maybe too long, wrong characters, doesn't like your face). What I got was:" + commandRaw + "start character was" + commandRaw[0] );
    return ""; //Return nothing since the command wasnt legit.
  }
  // now we remove the start and end characters
  commandRaw.remove(0,1);
  commandRaw.remove(commandRaw.length()-1,1);
  // remove a proceeding delimter if there is one there
  if (commandRaw.startsWith(delimeter)){
    commandRaw.remove(0,1);
  }
  //remove an ending delimeter if there is one there
  if (commandRaw.endsWith(delimeter)){
    commandRaw.remove(commandRaw.length()-1,1);
  }

  return commandRaw; //This is then a string of arguments like command/arg0/arg1/arg2.../argN
}



String getSerial(){
  // Function to get the serial data from the buffer if there is any available
  if (Serial.available() > 0) { // Check if there is data available to read - it should be since you should already have done thi.
    // Note in folloing line we need [0] to extract first character since readBytesUntil deals with characters.
    
    Serial.readBytesUntil(transmission_stop[0], incomingCommand, max_serial_length_chars); // Read the incoming data  
    
    receivedCommand = String(incomingCommand); // Convert the char array to a String
    
    
    
    return receivedCommand;
  } else {
    return "";
  }
}






String argGetString(String arguments, int position) {
  // Function for extracting an argument at position from a given string.
  // It takes a string like command/arg0/arg1/arg2.../argN and returns the argument at position N, I.e. if you input 2 it would return a string "arg2"
  // If you input position=-1 it will return the command itself.
  int currentIndex = 0;
  for (int i = 0; i <= position; ++i) {
    currentIndex = arguments.indexOf(delimeter, currentIndex);
    if (currentIndex == -1) {
      // The command does not have enough arguments so we should return nothing.
      raiseError("Attempted to access an argument that does not exist for the command you sent.");
      return "";
    }

      // Keep going until we reach the right delimeter.
      currentIndex++; //Note in the last iteration this will put us to thefirst letter of the argument string

  }
  // Now we have found the location of the correct delimeter

  int nextIndex = arguments.indexOf(delimeter, currentIndex);
  if (nextIndex == -1) { //In case there are no more delimeters i.e. we are on the last argument.
    // This is the last argument
    nextIndex = arguments.length();
  }

  return arguments.substring(currentIndex, nextIndex);
}


String getCommand(String commandRaw){
  // Function to extract the command from a string of arguments like command/arg0/arg1/arg2.../argN
  return argGetString(commandRaw,-1);//-1 is so that when we go into aGet it gets the first thing which is the command itself.
}

float argGetFloat(String arguments, int position) {
  String str = argGetString(arguments, position); // Get the argument as a string first

  char charBuf[50]; // Create a buffer to hold the characters of the string
  str.toCharArray(charBuf, 50); // Convert the string to a char array. Note this will add a null character \0 at the end of the array

  // Check if the string represents a number in scientific notation
  char *endptr;
  float value = strtof(charBuf, &endptr); //Note if this succeeeds endptr will be the null character. If it fails it would be some other arbitrary character.

  if (*endptr != '\0' || endptr == charBuf) { // We now check if the endptr is not the null character or if it is the same as the start of the string. If either of these are true then we have an error.
    raiseError("Expected a float in argument " + String(position) + " of command " + getCommand(arguments) + " but got " + str + " instead.");
    return NAN; // Return NaN to indicate an error
  } else {
    return value; // Return the float
  }
}



int argGetInt(String arguments, int position) {
  String str = argGetString(arguments, position); // Get the argument as a string first

  char charBuf[50]; // Create a buffer to hold the characters of the string
  str.toCharArray(charBuf, 50); // Convert the string to a char array. Note this will add a null character \0 at the end of the array

  // Check if the string represents a number
  char *endptr;
  long value = strtol(charBuf, &endptr, 10); // Convert the string to a long integer

  if (*endptr != '\0' || endptr == charBuf) { // We now check if the endptr is not the null character or if it is the same as the start of the string. If either of these are true then we have an error.
    raiseError("Expected an INT in argument " + String(position) + " of command " + getCommand(arguments) + " but got " + str + " instead.");
    return 0; // Return unhappy
  } else if (String(value) != str) { // We now check if the string representation of the integer is the same as the original string. If not then we have an error most likely that the integer wrapped.
    raiseError("You sent an integer " + str + " which is too large to fit in a long int data type so it wrapped. This is basically a random number generator so fix it!.");
    return 0; // Return unhappy
  } else {
    return int(value); // Return the integer
  }

}

bool argGetBool(String arguments, int position){
  // Function to get the nth argument as a BOOL from a decoded command string
  // Note this will return true if the argument is "true" or "True" or "TRUE" or "1" and false otherwise.
  String str = argGetString(arguments, position); // Get the argument as a string first
  if (str == "true" || str == "True" || str == "TRUE" || str == "1"){
    return true;
  } else if (str == "false" || str == "False" || str == "FALSE" || str == "0"){
    return false;
  } else {
    raiseError("Expected a BOOL in argument " + String(position) + " of command " + getCommand(arguments) + " but got " + str + " instead.");
    return NAN; // Return unhappy
  }
}


////////////////// misc Functions ///////////////////////


void raiseError(String errorString){
  // Function to raise an error and send it back to the PC
  String out;
  out=serialEncodeCmd(error_command,errorString);
  
  
  serialTransmit(out,false,true); // Note the wait flag is set false so error commands always go out.
  
}
