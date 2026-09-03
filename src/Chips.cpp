#include "Chips.h"

#include <SPI.h>
#include <Wire.h>

#include "Pins.h"
#include "digitalWriteFast.h"

namespace chips {
namespace {

void i2cWrite(uint8_t address, const uint8_t* data, size_t length) {
  Wire.beginTransmission(address);
  for (size_t i = 0; i < length; i++) Wire.write(data[i]);
  Wire.endTransmission();
}

void i2cWrite2(uint8_t address, uint8_t b0, uint8_t b1) {
  const uint8_t data[2] = {b0, b1};
  i2cWrite(address, data, 2);
}

void i2cRead(uint8_t address, uint8_t* buffer, size_t count) {
  Wire.requestFrom(address, count);
  size_t i = 0;
  while (Wire.available() && i < count) buffer[i++] = Wire.read();
}

void spiTransfer4(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3) {
  digitalWriteFast(pins::kSpiCs0, LOW);
  SPI.transfer(b0);
  SPI.transfer(b1);
  SPI.transfer(b2);
  SPI.transfer(b3);
  digitalWriteFast(pins::kSpiCs0, HIGH);
}

uint16_t voltsToCode16(float volts) {
  if (volts < 0.0f) volts = 0.0f;
  if (volts > 3.3f) volts = 3.3f;
  return (uint16_t)((volts / 3.3f) * 65535.0f);
}

}  // namespace

uint8_t probe(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission();
}

size_t i2cScan(uint8_t* found, size_t maxFound) {
  size_t n = 0;
  for (uint8_t address = 1; address < 127 && n < maxFound; address++) {
    if (probe(address) == 0) found[n++] = address;
  }
  return n;
}

void pca9685Init(uint8_t address, bool slowClock) {
  i2cWrite2(address, 0x00, 0x11);  // MODE1: sleep (required before prescale)
  // Prescale: 0x03 -> 1526 Hz. The switch chip runs slow (0x1e -> 200 Hz)
  // because the 12 V switches have ~1 ms turn-on time and would otherwise
  // respond very non-linearly to PWM.
  i2cWrite2(address, 0xFE, slowClock ? 0x1E : 0x03);
  i2cWrite2(address, 0x00, 0x01);  // MODE1: wake, all-call on, restart off
}

void pca9685SetDuty(uint8_t address, int channel, float duty) {
  // Register layout: LEDn_ON_L starts at 0x06 for channel 1 in our 1-based
  // numbering (0x06 = 2 + 4*1); channel 0 uses the ALL_LED registers.
  uint8_t onL, onH, offL, offH;
  if (channel == 0) {
    onL = 0xFA; onH = 0xFB; offL = 0xFC; offH = 0xFD;
  } else {
    onL = (uint8_t)(2 + 4 * channel);
    onH = (uint8_t)(3 + 4 * channel);
    offL = (uint8_t)(4 + 4 * channel);
    offH = (uint8_t)(5 + 4 * channel);
  }

  if (duty >= 1.0f) {
    i2cWrite2(address, onH, 0x10);  // full-on bit
    i2cWrite2(address, onL, 0x00);
  } else {
    i2cWrite2(address, onL, 0x00);
    i2cWrite2(address, onH, 0x00);  // clear full-on bit
  }
  const int off = (int)(duty * 4096.0f) % 4096;
  i2cWrite2(address, offL, (uint8_t)(off & 0xFF));
  i2cWrite2(address, offH, (uint8_t)((off >> 8) & 0xFF));
}

uint16_t ads7828Read(uint8_t address, int channel) {
  // Single-ended channel select bits (datasheet table 1: the channel order
  // is interleaved), plus power-down bits 0x4 = external ref, ADC on.
  static const uint8_t kChannelBits[8] = {0x8, 0xC, 0x9, 0xD, 0xA, 0xE, 0xB, 0xF};
  const uint8_t cmd = (uint8_t)((kChannelBits[channel & 0x7] << 4) | 0x4);
  i2cWrite(address, &cmd, 1);
  uint8_t buffer[2] = {0, 0};
  i2cRead(address, buffer, 2);
  return (uint16_t)((buffer[0] << 8) | buffer[1]);
}

void ad5668Setup(bool powerOn) {
  // Static control pins: LDAC held low (outputs update on write), CLR high.
  digitalWriteFast(pins::kDacLdacBar, LOW);
  digitalWriteFast(pins::kDacClrBar, HIGH);

  if (powerOn) {
    spiTransfer4(0x08, 0x00, 0x00, 0x00);  // reference: external
    spiTransfer4(0x05, 0x00, 0x00, 0x00);  // clear code: zero scale
    spiTransfer4(0x04, 0x00, 0x00, 0xFF);  // power mode: normal, all channels
    spiTransfer4(0x07, 0xF0, 0x00, 0x00);  // software reset, all channels
  } else {
    // Power down all channels through 10 kOhm to ground.
    spiTransfer4(0x04, 0x00, 0x02, 0xFF);
  }
}

void ad5668Set(int channel, float volts) {
  const uint8_t address = (channel == 0) ? 0x0F : (uint8_t)(channel - 1);
  const uint16_t code = voltsToCode16(volts);
  // Command 0x03: write to input register and update output.
  spiTransfer4(0x03,
               (uint8_t)((address << 4) | ((code >> 12) & 0x0F)),
               (uint8_t)((code >> 4) & 0xFF),
               (uint8_t)((code & 0x0F) << 4));
}

void ad5669Setup(uint8_t i2cAddress, bool powerOn) {
  {
    // Power down all channels (100 kOhm to ground) as a known state.
    const uint8_t data[3] = {0x40, 0x02, 0xFF};
    i2cWrite(i2cAddress, data, 3);
  }
  {
    // Reference: external.
    const uint8_t data[3] = {0x80, 0x00, 0x00};
    i2cWrite(i2cAddress, data, 3);
  }
  if (powerOn) {
    const uint8_t data[3] = {0x40, 0x00, 0xFF};
    i2cWrite(i2cAddress, data, 3);
  }
}

void ad5669Set(uint8_t i2cAddress, int channel, float volts) {
  const uint8_t address = (channel == 0) ? 0x0F : (uint8_t)(channel - 1);
  const uint16_t code = voltsToCode16(volts);
  // Command 0x30: write to input register and update output.
  const uint8_t data[3] = {(uint8_t)(0x30 | address),
                           (uint8_t)((code >> 8) & 0xFF),
                           (uint8_t)(code & 0xFF)};
  i2cWrite(i2cAddress, data, 3);
}

}  // namespace chips
