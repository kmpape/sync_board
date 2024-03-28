#ifndef SERIALCONTROLLER_H
#define SERIALCONTROLLER_H

#include "Arduino.h"





/**
 *  @file SerialController
 * @brief This module handles all serial communication with the PC.
*/

/**
 * @brief Function to get the serial input from the serial buffer. This is first thing you call when handling a serial command. It reads one line up to the end_transmission character.
 * @return The serial input as a string
*/
String getSerial(); // Function to get the serial input from the serial buffer


/**
 * @brief Function to decode a command from serial
 * @param commandString The string to decode - this has arrived raw from serial. Format should be $command/arg1/arg2/arg3# (where / is the delimeter)
 * @return The decoded string
*/
String serialDecode(String commandString); // Function to decode a command from serial

/**
 * @brief Function to get the command from a decoded command string
 * @param commandString The decoded string, should be formatted command/arg1/arg2/arg3
 * @return The command coming back
*/
String getCommand(String commandString); // Function to get the command from a decoded command string

/**
 * @brief Function to get the nth argument as a STRING from a decoded command string
 * @param commandString The decoded string, should be formatted command/arg1/arg2/arg3
 * @param argNumber That argument as a string
 * @return The argument as a string
*/
String argGetString(String commandString, int argNumber); // Function to get the nth argument as a STRING from a decoded command string

/** 
 * @brief Function to get the nth argument as a FLOAT from a decoded command string. Note if you send an integer it will get converted to float.
 * @param commandString The decoded string, should be formatted command/arg1/arg2/arg3
 * @param argNumber That argument as a float
 * @return The argument as a float
*/
float argGetFloat(String commandString, int argNumber); // Function to get the nth argument as a FLOAT from a decoded command string

/**
 * @brief Function to get the nth argument as an INT from a decoded command string. Note if you sent a (decimal) float it will error.
 * @param commandString The decoded string, should be formatted command/arg1/arg2/arg3
 * @param argNumber That argument as an int
 * @return The argument as a int
*/
int argGetInt(String commandString, int argNumber); // Function to get the nth argument as an INT from a decoded command string

/**
 * @brief Function to get the nth argument as a BOOL from a decoded command string. You can send it as True/true/TRUE/1 or False/false/FALSE/0
 * @param commandString The decoded string, should be formatted command/arg1/arg2/arg3
 * @param argNumber That argument as a bool
 * @return The argument as a bool
*/
bool argGetBool(String commandString, int argNumber); // Function to get the nth argument as a BOOL from a decoded command string


/**
 * @brief Function to raise an error and send it over serial
 * @param errorString The error string to send
*/
void raiseError(String errorString); // Function to raise an error and send it over serial

/**
 * @fn bool serialSend(const String& command, Args... args);
 * @brief Function to send a command over serial
 * @param command The command to send as String
 * @param arg1 The first argument to send. Can be a String, int, bool, and if a float will be converted to scientific notation.
 * @param arg2 The second argument to send
 * @param arg3 The third argument to send - you can keep adding arbitrary numbers of arguments of an arbitrary type.
 * @return True if successful, false if not
*/



/**
 * @brief Function to send a command over serial
 * @param command The command to send as String
 * @param array The array to send
 * @param arrayLength The length of the array 
 * @return True if successful, false if not
*/
bool serialSendData(const String& command, float* array, size_t arrayLength); // Used to send data arrays
























/////////////////// DONT USE BELOW COMMANDS _ FOR ENCODING MESSAGES ///////////////////////
//Below is a big mess since we are using template functions so you have them all fully defined in here. If you edit the cpp file you will need to edit here. 
// Note if you edit any of the below you ALSO need to edit them in cpp.

bool serialTransmit(String commandString, bool wait=true,bool iserror=false); // SHOULDNT be used outside SerialController. Function to transmit a command over serial - requres already encoded command.

extern String start_character; //Defie these from the cpp file so the below can use them.
extern String end_character;
extern String transmission_stop;
extern String delimeter;
extern int data_transmission_sigfigs;
String toString(float arg); // Helper function for converting to string - defined in cpp











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



#endif