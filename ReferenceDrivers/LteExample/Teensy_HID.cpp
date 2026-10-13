#include "Teensy_HID.h"
#include "machine.h"

TeensyHID teensy_device;

// config file can be found here:
// /Users/caleb/.platformio/packages/framework-arduinoteensy/cores/teensy4/usb_desc.h

TeensyHID::TeensyHID() {
    // initialise the error_packet
    // initialiseErrorPacket();
        // Return an empty packet if no response
}

void TeensyHID::begin() {
    // usb_rawhid_configure();
}

// Max size 49 characters of text, otherwise it will auto split it into multiple messages
void TeensyHID::sendInfo(const char* data, ...){
    if (send_info_packets) {

        va_list args;
        va_list ap_copy;
        
        va_start(args, data);
        va_copy(ap_copy, args);

        int input_message_size = vsnprintf(NULL, 0, data, ap_copy);
        va_end(ap_copy);

        int number_of_messages_needed = std::ceil(float(input_message_size) / float(INFO_SIZE-9)); // leave space for prepend message number/total and null terminator
        // sendError("Need %d messages to send info", number_of_messages_needed);

        char holding_buffer[input_message_size + 1];
        vsnprintf(holding_buffer, input_message_size + 1, data, args);
        va_end(args);
        // sendError("Holding size is %d first char %c, end char: %c", holding_size, holding_buffer[0], holding_buffer[holding_size-1]);

        for (int i=0; i<number_of_messages_needed; i++){
            info_packet = {};
            info_packet.type = PacketType_INFO;
            info_packet.has_data = true;
            info_packet.data.has_info = true;

            // Calculate the chunk size for this message (handle last chunk potentially being smaller
            snprintf(info_packet.data.info, INFO_SIZE, "%3d/%-3d %s", i+1, number_of_messages_needed, holding_buffer + i*(INFO_SIZE-9));

            sendPacket(&info_packet);
        }
    }
}

// Max size 49 characters of text, otherwise it will auto split it into multiple messages
void TeensyHID::sendError(const char* data, ...){
    if (send_error_packets) {
        
        va_list args;
        va_list ap_copy;
        
        va_start(args, data);
        va_copy(ap_copy, args);

        int input_message_size = vsnprintf(NULL, 0, data, ap_copy);
        va_end(ap_copy);

        int number_of_messages_needed = std::ceil(float(input_message_size) / float(INFO_SIZE-9)); // leave space for prepend message number/total and null terminator
        // sendError("Need %d messages to send info", number_of_messages_needed);

        char holding_buffer[input_message_size + 1];
        vsnprintf(holding_buffer, input_message_size + 1, data, args);
        va_end(args);
        // sendError("Holding size is %d first char %c, end char: %c", holding_size, holding_buffer[0], holding_buffer[holding_size-1]);

        for (int i=0; i<number_of_messages_needed; i++){
            error_packet = {};
            error_packet.type = PacketType_ERROR;
            error_packet.has_data = true;
            error_packet.data.has_info = true;

            // Calculate the chunk size for this message (handle last chunk potentially being smaller
            snprintf(error_packet.data.info, INFO_SIZE, "%3d/%-3d %s", i+1, number_of_messages_needed, holding_buffer + i*(INFO_SIZE-9));

            sendPacket(&error_packet);
        }
    }
}

// Max size 49 characters of text, otherwise it will auto split it into multiple messages
void TeensyHID::sendDebug(const char* data, ...){
    if (send_debug_packets){

        va_list args;
        va_list ap_copy;
        
        va_start(args, data);
        va_copy(ap_copy, args);

        int input_message_size = vsnprintf(NULL, 0, data, ap_copy);
        va_end(ap_copy);

        int number_of_messages_needed = std::ceil(float(input_message_size) / float(INFO_SIZE-9)); // leave space for prepend message number/total and null terminator
        // sendError("Need %d messages to send info", number_of_messages_needed);

        char holding_buffer[input_message_size + 1];
        vsnprintf(holding_buffer, input_message_size + 1, data, args);
        va_end(args);
        // sendError("Holding size is %d first char %c, end char: %c", holding_size, holding_buffer[0], holding_buffer[holding_size-1]);

        for (int i=0; i<number_of_messages_needed; i++){
            debug_packet = {};
            debug_packet.type = PacketType_DEBUG;
            debug_packet.has_data = true;
            debug_packet.data.has_info = true;

            // Calculate the chunk size for this message (handle last chunk potentially being smaller
            snprintf(debug_packet.data.info, INFO_SIZE, "%3d/%-3d %s", i+1, number_of_messages_needed, holding_buffer + i*(INFO_SIZE-9));

            sendPacket(&debug_packet);
        }
    }
}

void TeensyHID::clearBuffer(){
    int cleared_count = 0;
    const int max_clear_attempts = 1000; // Safety limit to prevent infinite loop
    
    while (receiveBuffer(incoming_buffer) > 0 && cleared_count < max_clear_attempts) {
        cleared_count++;
    }
    
    if (cleared_count > 0) {
        sendInfo("Cleared %d packets from buffer", cleared_count);
    }
    
    if (cleared_count >= max_clear_attempts) {
        sendError("Buffer clear hit safety limit - may have residual data");
    }
}

int8_t TeensyHID::receiveBuffer(uint8_t* buffer) {
    // Checks to see if anything is on the incoming USB buffer, and writes it if there is
    // returns the number of bytes received
    int8_t recv_state = RawHID.recv(buffer, 1); 
    return recv_state;

}

uint8_t TeensyHID::parseBuffer(Packet* packet_input, uint8_t* buffer) {
    uint8_t size = buffer[0];

    uint8_t msg_bytes[size];
    // copy [size] bytes from memory position [buffer+1] to the msg_bytes array
    memcpy(msg_bytes, buffer+1, size);

    pb_istream_t stream = pb_istream_from_buffer(msg_bytes, size);
    
    pb_decode(&stream, Packet_fields, packet_input);

    return true;
}

int8_t TeensyHID::sendPacket(Packet* packet) {

    uint8_t outgoing_buffer[PACKET_SIZE] = {0};
    pb_ostream_t output_stream = pb_ostream_from_buffer(outgoing_buffer, PACKET_SIZE);
    
    bool encode_success = pb_encode(&output_stream, Packet_fields, packet);
    if (!encode_success) {
        sendError(output_stream.errmsg);
    }

    if (output_stream.bytes_written > PACKET_SIZE - 1) { // if the packet to be sent is bigger than the available packet space i.e. 63 bytes (1 byte reserved for size data)
        sendError("Packet too large to send, UID:%d", packet->unique_id);
    } else {
        memmove(outgoing_buffer + 1, outgoing_buffer, output_stream.bytes_written); //shift the message forward one position
        uint8_t padding_size = PACKET_SIZE - output_stream.bytes_written - 1; // calculate the padding size needed
        outgoing_buffer[0] = output_stream.bytes_written; // set the first byte to the message size
        memset(1 + outgoing_buffer + output_stream.bytes_written, 0, padding_size); // pad the remaining message with 0s
    }

    int8_t send_state = RawHID.send(outgoing_buffer, 1);
    // initialiseErrorPacket(); // reset the error packet
    return true;
}

int8_t TeensyHID::receivePacket(Packet* received_packet) {

    int8_t response = receiveBuffer(incoming_buffer);

    if (response > 0) {
        parseBuffer(received_packet, incoming_buffer);
    }
    return response;
}
