#ifndef TEENSY_HID_H
#define TEENSY_HID_H

#include <Arduino.h>
#include <usb_rawhid.h>

extern "C" {
  #include "pb_encode.h"
  #include "pb_decode.h"
  #include <packet_config.pb.h>
}
#include <map>

#define PACKET_SIZE 64

static constexpr size_t INFO_SIZE = sizeof(RequestedData::info);

class TeensyHID {
  public:
    bool send_error_packets = true; // Flag to indicate if error packets should be sent
    bool send_info_packets = true; // Flag to indicate if info packets should be sent
    bool send_debug_packets = false; // Flag to indicate if debug packets should be sent
    TeensyHID();
    void begin();
    void sendError(const char* data, ...);
    void sendInfo(const char* data, ...);
    void sendDebug(const char* data, ...);
    // void sendData(SystemName system, uint8_t data_type, uint64_t unique_id, auto data_value, bool close=false);
    // void initialiseErrorPacket();

    // Uses receiveBuffer to read the incoming buffer and create a packet
    int8_t receivePacket(Packet* received_packet);

    void clearBuffer(); // clears the incoming buffer

    // checks to see if there is an incoming message of 64 bytes
    // returns 1 if there is a message, 0 if there is not
    int8_t receiveBuffer(uint8_t* buffer);

    // creates a packet from the input buffer
    uint8_t parseBuffer(Packet* packet_input, uint8_t* buffer);

    // sends a packet to the computer
    int8_t sendPacket(Packet* packet_output);
    
    // bool has_error = false;

  private:
    char errors[sizeof(RequestedData::info)];

    uint8_t incoming_buffer[64];
    
    uint16_t crc16_ccitt(uint8_t* data, uint16_t length);

    Packet error_packet = Packet_init_zero;
    Packet debug_packet = Packet_init_zero;
    Packet info_packet = Packet_init_zero;
};

extern TeensyHID teensy_device; // create the communication object that can be used globally


#endif  // TEENSY_HID_H