#pragma once

// Teensy 4.1 pin map for the SyncBoard.
// Cross-reference SyncBoard1.SchDoc in the schematics for the net names.

namespace pins {

// Heartbeat. Goes to Expansion 1+2 headers + LED + Magnet boards, where it
// acts as a liveness signal: attached boards power down if it stops toggling.
constexpr int kHeartbeat = 41;

// Fixed digital IO (d0..d7 on schematic).
constexpr int kDigitalIn[4] = {1, 2, 3, 4};
constexpr int kDigitalOut[4] = {5, 6, 7, 8};

// SPI (bus 0). SCK doubles as the Teensy indicator LED.
constexpr int kSpiCs0 = 10;   // chip select for the on-board AD5668 DAC
constexpr int kSpiMosi = 11;
constexpr int kSpiMiso = 12;
constexpr int kSpiSck = 13;
constexpr int kSpiCs1 = 0;    // goes to expansion headers / display counter

// I2C (bus 0). Shared with expansion/LED/magnet boards.
constexpr int kI2cSda = 18;
constexpr int kI2cScl = 19;

// AD5668 DAC control pins.
constexpr int kDacLdacBar = 21;  // active-low LDAC (held low: outputs update immediately)
constexpr int kDacClrBar = 20;   // active-low CLR

// Camera interface (Kinetix naming in comments).
constexpr int kCameraTriggerReady = 25;  // camera ready for next trigger (Tr0)
constexpr int kCameraTriggerIn = 32;     // our trigger output to the camera (TrI)
constexpr int kCameraReading = 28;       // camera reading out data (RO)
constexpr int kCameraLed[4] = {31, 29, 27, 26};  // camera "activate LED n" outputs

// LED board enable lines, channels 1..8 (d16..d23 on schematic).
constexpr int kLedEnable[8] = {33, 34, 35, 36, 37, 38, 39, 40};

}  // namespace pins
