// SyncBoard firmware: command dispatch and the main loop.
//
// Everything here is a thin protocol layer; the actual behaviour lives in
// the subsystem modules (Io, Leds, Magnet, Signals, Imaging, System).

#include <Arduino.h>

#include "Calibration.h"
#include "Chips.h"
#include "Imaging.h"
#include "Io.h"
#include "Leds.h"
#include "Magnet.h"
#include "Pins.h"
#include "Protocol.h"
#include "Signals.h"
#include "State.h"
#include "System.h"
#include "digitalWriteFast.h"

namespace {

using protocol::argBool;
using protocol::argFloat;
using protocol::argInt;
using protocol::fault;
using protocol::faultPending;

constexpr uint32_t kHeartbeatPeriodMs = 100;

bool requireEnabled() {
  if (!gSystemEnabled) {
    fault("system is disabled; send 'enable' first");
    return false;
  }
  return true;
}

bool requireDisabled() {
  if (gSystemEnabled) {
    fault("not allowed while the system is enabled; send 'disable' first");
    return false;
  }
  return true;
}

bool requireLed() {
  if (!requireEnabled()) return false;
  if (!gLedAttached) {
    fault("LED board not attached (send attachLed/1 before enabling)");
    return false;
  }
  return true;
}

// ---- System ----------------------------------------------------------------

void cmdPing() {
  protocol::beginOk();
  protocol::addStr("syncboard");
  protocol::addStr(kFirmwareVersion);
  protocol::addUint(millis());
  protocol::endReply();
}

void cmdStatus() {
  protocol::beginOk();
  protocol::addInt(gSystemEnabled ? 1 : 0);
  protocol::addInt(gLedAttached ? 1 : 0);
  protocol::addInt(gMagnetAttached ? 1 : 0);
  protocol::addInt(imaging::syncMode());
  protocol::endReply();
}

void cmdEnable() { system_::setEnabled(true); }
void cmdDisable() { system_::setEnabled(false); }
void cmdResetConfig() { system_::resetConfig(); }

void cmdFactoryReset() {
  calibration::factoryReset();
  protocol::logf("EEPROM cleared; all LED calibrations lost");
}

void cmdAttachLed() {
  const bool present = argBool(0);
  if (faultPending() || !requireDisabled()) return;
  gLedAttached = present;
}

void cmdAttachMagnet() {
  const bool present = argBool(0);
  if (faultPending() || !requireDisabled()) return;
  gMagnetAttached = present;
}

void cmdScanI2c() {
  if (!requireEnabled()) return;
  uint8_t found[16];
  const size_t n = chips::i2cScan(found, 16);
  protocol::beginOk();
  protocol::addUint((uint32_t)n);
  for (size_t i = 0; i < n; i++) protocol::addUint(found[i]);
  protocol::endReply();
}

// ---- Basic IO --------------------------------------------------------------

void cmdReadDi() {
  const int ch = argInt(0);
  if (faultPending() || !requireEnabled()) return;
  const bool value = io::readDigitalIn(ch);
  if (faultPending()) return;
  protocol::beginOk();
  protocol::addInt(value ? 1 : 0);
  protocol::endReply();
}

void cmdWriteDo() {
  const int ch = argInt(0);
  const bool high = argBool(1);
  if (faultPending() || !requireEnabled()) return;
  io::writeDigitalOut(ch, high);
}

void cmdPulseDo() {
  const int ch = argInt(0);
  const float ms = argFloat(1);
  if (faultPending() || !requireEnabled()) return;
  io::pulseDigitalOut(ch, ms);
}

void cmdSetupGpio() {
  const int label = argInt(0);
  const bool enabled = argBool(1);
  const bool isInput = argBool(2);
  if (faultPending() || !requireDisabled()) return;
  const int index = io::gpioIndexFromLabel(label);
  if (index < 0) return;
  io::setGpioConfig(index, enabled, isInput);
}

void cmdWriteGpio() {
  const int label = argInt(0);
  const bool high = argBool(1);
  if (faultPending() || !requireEnabled()) return;
  const int index = io::gpioIndexFromLabel(label);
  if (index < 0) return;
  io::writeGpio(index, high);
}

void cmdReadGpio() {
  const int label = argInt(0);
  if (faultPending() || !requireEnabled()) return;
  const int index = io::gpioIndexFromLabel(label);
  if (index < 0) return;
  const bool value = io::readGpio(index);
  if (faultPending()) return;
  protocol::beginOk();
  protocol::addInt(value ? 1 : 0);
  protocol::endReply();
}

void cmdReadAdc() {
  const int ch = argInt(0);
  // Optional second argument selects the board: 0 sync (default), 1 LED, 2 magnet.
  const int adcId = (protocol::argCount() >= 2) ? argInt(1) : 0;
  if (faultPending() || !requireEnabled()) return;
  const float volts = io::readAdcVolts(ch, adcId);
  if (faultPending()) return;
  protocol::beginOk();
  protocol::addFloat(volts);
  protocol::endReply();
}

void cmdSetDac() {
  const int ch = argInt(0);
  const float volts = argFloat(1);
  if (faultPending() || !requireEnabled()) return;
  io::setDacVolts(ch, volts);
}

void cmdSetSwitch() {
  const int ch = argInt(0);
  const float duty = argFloat(1);
  if (faultPending() || !requireEnabled()) return;
  io::setSwitch(ch, duty);
}

// ---- LEDs ------------------------------------------------------------------

void cmdCalibrateLed() {
  const int ch = argInt(0);
  const float maxCurrent = argFloat(1);
  if (faultPending() || !requireLed()) return;
  if (!(maxCurrent > 0.0f && maxCurrent <= 15.0f)) {
    fault("max current %.2f A out of range (0, 15]", (double)maxCurrent);
    return;
  }
  leds::calibrate(ch, maxCurrent);
}

void cmdSetLedLevel() {
  const int ch = argInt(0);
  const float level = argFloat(1);
  const int feedback = argInt(2);  // 0 = current, 1 = optical power
  if (faultPending() || !requireLed()) return;
  if (feedback != 0 && feedback != 1) {
    fault("feedback mode %d not recognised (0 = current, 1 = optical)", feedback);
    return;
  }
  leds::setLevel(ch, level, feedback == 0);
}

void cmdSwitchLed() {
  const int ch = argInt(0);
  const bool on = argBool(1);
  if (faultPending() || !requireLed()) return;
  leds::switchOn(ch, on);
}

void cmdSwitchLedTimed() {
  const int ch = argInt(0);
  const float ms = argFloat(1);
  if (faultPending() || !requireLed()) return;
  leds::switchTimed(ch, ms);
}

void cmdDisableLeds() {
  if (!requireLed()) return;
  leds::allOff();
}

void cmdMeasureLed() {
  const int ch = argInt(0);
  if (faultPending() || !requireLed()) return;
  float result[2];
  leds::measure(ch, result);
  if (faultPending()) return;
  protocol::beginOk();
  protocol::addFloat(result[0]);
  protocol::addFloat(result[1]);
  protocol::endReply();
}

void cmdMeasurePhotodiode() {
  const int ch = argInt(0);
  if (faultPending() || !requireLed()) return;
  const float mv = leds::measurePhotodiode(ch);
  if (faultPending()) return;
  protocol::beginOk();
  protocol::addFloat(mv);
  protocol::endReply();
}

void cmdGetLedSetup() {
  const int ch = argInt(0);
  if (faultPending() || !requireLed()) return;
  float result[4];
  leds::getSetup(ch, result);
  if (faultPending()) return;
  protocol::beginOk();
  for (int i = 0; i < 4; i++) protocol::addFloat(result[i]);
  protocol::endReply();
}

// ---- Magnet ----------------------------------------------------------------

void cmdSetupMagnet() {
  if (!requireEnabled()) return;
  magnet::setup();
}

void cmdEnableMagnet() {
  const bool on = argBool(0);
  if (faultPending() || !requireEnabled()) return;
  magnet::enable(on);
}

void cmdSelectMagnetOutput() {
  const bool nc = argBool(0);
  if (faultPending() || !requireEnabled()) return;
  magnet::selectOutput(nc);
}

void cmdCalibrateMagnet() {
  if (!requireEnabled()) return;
  magnet::calibrate();
}

void cmdCalibrateHall() {
  const int id = argInt(0);
  if (faultPending() || !requireEnabled()) return;
  magnet::calibrateHall(id);
}

void cmdSetMagnetCurrent() {
  const bool nc = argBool(0);
  const float value = argFloat(1);
  if (faultPending() || !requireEnabled()) return;
  magnet::setCurrent(nc, value);
}

void cmdSetMagnetField() {
  const bool nc = argBool(0);
  const float mT = argFloat(1);
  if (faultPending() || !requireEnabled()) return;
  magnet::setField(nc, mT);
}

void cmdReadHall() {
  const int id = argInt(0);
  if (faultPending() || !requireEnabled()) return;
  const float mT = magnet::readHallMilliTesla(id);
  if (faultPending()) return;
  protocol::beginOk();
  protocol::addFloat(mT);
  protocol::endReply();
}

void cmdReadMagnetAdc() {
  const int ch = argInt(0);
  if (faultPending() || !requireEnabled()) return;
  const float volts = magnet::readAdcVolts(ch);
  if (faultPending()) return;
  protocol::beginOk();
  protocol::addFloat(volts);
  protocol::endReply();
}

void cmdSetMagnetDac() {
  const int ch = argInt(0);
  const float volts = argFloat(1);
  if (faultPending() || !requireEnabled()) return;
  magnet::setDacVolts(ch, volts);
}

// ---- Signals ---------------------------------------------------------------

void cmdSetupSignal() {
  const int index = argInt(0);
  const int mode = argInt(1);
  const int option = argInt(2);
  const bool repeat = argBool(3);
  const bool isSlave = argBool(4);
  if (faultPending()) return;
  if (mode < 0 || option < 0) {
    fault("mode and option must be non-negative");
    return;
  }
  signals::configure(index, (signals::Mode)mode, (uint32_t)option, repeat, isSlave);
}

void cmdLoadSignal() {
  static float values[signals::kMaxLength];
  static float delays[signals::kMaxLength];
  const int index = argInt(0);
  const int count = argInt(1);
  if (faultPending()) return;
  if (count < 1 || count > signals::kMaxLength) {
    fault("signal length %d out of range 1..%d", count, signals::kMaxLength);
    return;
  }
  if ((int)protocol::argCount() != 2 + 2 * count) {
    fault("expected %d value/delay pairs but got %d extra arguments", count,
          (int)protocol::argCount() - 2);
    return;
  }
  for (int i = 0; i < count; i++) {
    values[i] = argFloat(2 + 2 * i);
    delays[i] = argFloat(3 + 2 * i);
  }
  if (faultPending()) return;
  signals::load(index, count, values, delays);
}

void cmdLoadSignalUniform() {
  const int index = argInt(0);
  const int count = argInt(1);
  const float intervalMs = argFloat(2);
  if (faultPending()) return;
  signals::loadUniform(index, count, intervalMs);
}

void cmdStartSignal() {
  const int index = argInt(0);
  if (faultPending() || !requireEnabled()) return;
  signals::start(index);
}

void cmdStopSignal() {
  const int index = argInt(0);
  if (faultPending()) return;
  signals::stop(index);
}

void cmdReadSignal() {
  const int index = argInt(0);
  if (faultPending() || !signals::validIndex(index)) return;
  const int n = signals::length(index);
  const float* data = signals::data(index);
  protocol::beginOk();
  protocol::addInt(n);
  for (int i = 0; i < n; i++) protocol::addFloat(data[i]);
  protocol::endReply();
}

// ---- Imaging ---------------------------------------------------------------

void cmdSetSyncMode() {
  const int mode = argInt(0);
  const bool ledByCamera = argBool(1);
  if (faultPending()) return;
  imaging::setSyncMode(mode, ledByCamera);
}

void cmdSetupImaging() {
  bool active[imaging::kMaxImages];
  int led[imaging::kMaxImages];
  uint32_t exposureUs[imaging::kMaxImages];
  for (int i = 0; i < imaging::kMaxImages; i++) {
    active[i] = argBool(i * 3);
    led[i] = argInt(i * 3 + 1);
    const float ms = argFloat(i * 3 + 2);
    if (ms < 0.0f) {
      fault("frame %d exposure must be >= 0 ms", i);
      return;
    }
    exposureUs[i] = (uint32_t)(ms * 1000.0f);
  }
  if (faultPending()) return;
  imaging::setupSequence(active, led, exposureUs);
}

void cmdStartImaging() {
  const int n = argInt(0);
  if (faultPending() || !requireEnabled()) return;
  imaging::startSequence(n);
}

// ---- Dispatch --------------------------------------------------------------

struct CommandEntry {
  const char* name;
  void (*handler)();
};

constexpr CommandEntry kCommands[] = {
    {"ping", cmdPing},
    {"status", cmdStatus},
    {"enable", cmdEnable},
    {"disable", cmdDisable},
    {"resetConfig", cmdResetConfig},
    {"factoryReset", cmdFactoryReset},
    {"attachLed", cmdAttachLed},
    {"attachMagnet", cmdAttachMagnet},
    {"scanI2c", cmdScanI2c},
    {"readDi", cmdReadDi},
    {"writeDo", cmdWriteDo},
    {"pulseDo", cmdPulseDo},
    {"setupGpio", cmdSetupGpio},
    {"writeGpio", cmdWriteGpio},
    {"readGpio", cmdReadGpio},
    {"readAdc", cmdReadAdc},
    {"setDac", cmdSetDac},
    {"setSwitch", cmdSetSwitch},
    {"calibrateLed", cmdCalibrateLed},
    {"setLedLevel", cmdSetLedLevel},
    {"switchLed", cmdSwitchLed},
    {"switchLedTimed", cmdSwitchLedTimed},
    {"disableLeds", cmdDisableLeds},
    {"measureLed", cmdMeasureLed},
    {"measurePhotodiode", cmdMeasurePhotodiode},
    {"getLedSetup", cmdGetLedSetup},
    {"setupMagnet", cmdSetupMagnet},
    {"enableMagnet", cmdEnableMagnet},
    {"selectMagnetOutput", cmdSelectMagnetOutput},
    {"calibrateMagnet", cmdCalibrateMagnet},
    {"calibrateHall", cmdCalibrateHall},
    {"setMagnetCurrent", cmdSetMagnetCurrent},
    {"setMagnetField", cmdSetMagnetField},
    {"readHall", cmdReadHall},
    {"readMagnetAdc", cmdReadMagnetAdc},
    {"setMagnetDac", cmdSetMagnetDac},
    {"setupSignal", cmdSetupSignal},
    {"loadSignal", cmdLoadSignal},
    {"loadSignalUniform", cmdLoadSignalUniform},
    {"startSignal", cmdStartSignal},
    {"stopSignal", cmdStopSignal},
    {"readSignal", cmdReadSignal},
    {"setSyncMode", cmdSetSyncMode},
    {"setupImaging", cmdSetupImaging},
    {"startImaging", cmdStartImaging},
};

void dispatch() {
  protocol::beginRequestCycle();
  const char* name = protocol::command();
  for (const CommandEntry& entry : kCommands) {
    if (strcmp(name, entry.name) == 0) {
      entry.handler();
      if (protocol::faultPending() && !protocol::replied()) {
        protocol::replyErr(protocol::faultMessage());
      } else if (!protocol::replied()) {
        protocol::replyOk();
      }
      protocol::clearFault();
      return;
    }
  }
  protocol::replyErr("unknown command");
}

void tickHeartbeat() {
  static uint32_t lastToggleMs = 0;
  const uint32_t now = millis();
  if (now - lastToggleMs >= kHeartbeatPeriodMs) {
    digitalWriteFast(pins::kHeartbeat, !digitalReadFast(pins::kHeartbeat));
    lastToggleMs = now;
  }
}

}  // namespace

void setup() {
  Serial.begin(2000000);  // USB CDC: the rate is nominal
  leds::init();           // load calibration from EEPROM
  system_::setEnabled(false);  // known safe state
  protocol::logf("syncboard %s booted", kFirmwareVersion);
}

void loop() {
  if (protocol::poll()) dispatch();

  if (gSystemEnabled) {
    tickHeartbeat();
    imaging::tick();
    signals::tick();
    leds::tick();
    io::tickDoPulses();
  }

  // Errors raised outside a request (e.g. from the signal engine) have no
  // reply to ride on; surface them on the log channel instead.
  if (protocol::faultPending()) {
    protocol::logf("ERROR: %s", protocol::faultMessage());
    protocol::clearFault();
  }
}
