#include <Arduino.h>

class BufferPrint : public Print {
private:
    char* buffer;
    size_t size;
    size_t position;

public:
    BufferPrint(char* buf, size_t buf_size) : buffer(buf), size(buf_size), position(0) {
        if (buffer && size > 0) {
            buffer[0] = '\0';
        }
    }

    virtual size_t write(uint8_t c) override {
        // Skip carriage return and line feed characters
        if (c == '\r' || c == '\n') {
            return 1;  // Pretend we wrote it, but don't actually add it
        }
        
        if (buffer && position < size - 1) {
            buffer[position++] = c;
            buffer[position] = '\0';  // Keep it null-terminated
            return 1;
        }
        return 0;
    }

    size_t getLength() const {
        return position;
    }
};