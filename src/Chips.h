#pragma once
#include <Arduino.h>

// Low-level drivers for the I2C/SPI chips on the SyncBoard and its expansion
// boards. All register sequences here are proven on the real hardware except
// the ADS124S08's, which are checked against its datasheet only; change
// them only with a datasheet open.

namespace chips {

// I2C address book.
constexpr uint8_t kSyncPwmLevelShiftAddr = 0x60;  // PCA9685 driving level shifters
constexpr uint8_t kSyncPwmSwitchesAddr = 0x40;    // PCA9685 driving 12 V switches
constexpr uint8_t kSyncAdcAddr = 0x48;            // ADS7828 on the SyncBoard
constexpr uint8_t kLedAdcAddr = 0x49;             // ADS7828 on the LED board
constexpr uint8_t kLedDacAddr = 0x57;             // AD5669 on the LED board
constexpr uint8_t kLedPwmAddr = 0x50;             // PCA9685 on the LED board
constexpr uint8_t kMagAdcAddr = 0x4A;             // ADS7828 on the magnet board
constexpr uint8_t kMagDacAddr = 0x54;             // AD5669 on the magnet board
constexpr uint8_t kScreenerAdcAddr = 0x4B;        // ADS7828 on the screener board
constexpr uint8_t kScreenerPwmAddr = 0x51;        // PCA9685 on the screener board

// Returns 0 when a device ACKs at the address (Wire::endTransmission code).
uint8_t probe(uint8_t address);

// Scans 1..126; fills found[] up to maxFound. Returns the number found.
size_t i2cScan(uint8_t* found, size_t maxFound);

// PCA9685 16-channel PWM.
// channel 0 addresses all channels at once, 1..16 a single one.
// duty is 0.0 (off) .. 1.0 (fully on).
void pca9685Init(uint8_t address, bool slowClock = false);
void pca9685SetDuty(uint8_t address, int channel, float duty);

// ADS7828 12-bit 8-channel ADC. channel is 0..7; single-ended, external
// reference (the internal-reference option is not wired on this PCB).
// One read is ~500 us at the 100 kHz bus clock.
uint16_t ads7828Read(uint8_t address, int channel);

// AD5668 16-bit 8-channel SPI DAC on the SyncBoard.
// channel 0 sets all channels, 1..8 a single one; volts clamped to [0, 3.3].
void ad5668Setup(bool powerOn);
void ad5668Set(int channel, float volts);

// AD5669 16-bit 8-channel I2C DAC (LED and magnet boards).
void ad5669Setup(uint8_t address, bool powerOn);
void ad5669Set(uint8_t address, int channel, float volts);

// ADS124S08 24-bit 12-input SPI ADC on the screener board (chip select CS1,
// SPI mode 1, behind the board's SPI isolator). Its START pin is tied low
// and DRDY is not wired, so conversions are started by command and read
// back after a fixed wait.
void ads124s08Reset();  // RESET command, then waits for the restart
uint8_t ads124s08ReadReg(uint8_t reg);
void ads124s08WriteRegs(uint8_t firstReg, const uint8_t* values, size_t count);
// START command: one conversion in single-shot mode. The caller waits out
// the conversion time, then reads it with ads124s08ReadData.
void ads124s08Start();
// RDATA command: the signed 24-bit result of the last conversion.
int32_t ads124s08ReadData();

}  // namespace chips
