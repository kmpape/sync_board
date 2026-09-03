#include "Calibration.h"

#include <EEPROM.h>

#include <cstring>

namespace calibration {
namespace {

constexpr uint32_t kMagic = 0x53424332;  // "SBC2"
constexpr int kHeaderAddr = 0;
constexpr int kLedBaseAddr = 4;
constexpr int kNumLedChannels = 8;

LedCal ledCals[kNumLedChannels];

int ledAddr(int channel) { return kLedBaseAddr + (channel - 1) * (int)sizeof(LedCal); }

}  // namespace

void load() {
  uint32_t magic = 0;
  EEPROM.get(kHeaderAddr, magic);
  if (magic != kMagic) {
    memset(ledCals, 0, sizeof(ledCals));
    return;
  }
  for (int ch = 1; ch <= kNumLedChannels; ch++) {
    EEPROM.get(ledAddr(ch), ledCals[ch - 1]);
  }
}

LedCal& led(int channel) { return ledCals[channel - 1]; }

void storeLed(int channel) {
  // EEPROM.put only rewrites changed bytes, so the header write is cheap.
  EEPROM.put(kHeaderAddr, kMagic);
  EEPROM.put(ledAddr(channel), ledCals[channel - 1]);
}

void factoryReset() {
  memset(ledCals, 0, sizeof(ledCals));
  EEPROM.put(kHeaderAddr, (uint32_t)0);
  for (int ch = 1; ch <= kNumLedChannels; ch++) {
    EEPROM.put(ledAddr(ch), ledCals[ch - 1]);
  }
}

}  // namespace calibration
