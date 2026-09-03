#include "System.h"

#include <Arduino.h>

#include "Imaging.h"
#include "Io.h"
#include "Leds.h"
#include "Magnet.h"
#include "Signals.h"
#include "State.h"

bool gSystemEnabled = false;
bool gLedAttached = false;
bool gMagnetAttached = false;

namespace system_ {

namespace {

void teardownToSafeState() {
  // Stop all activity first so nothing re-drives an output mid-shutdown.
  signals::stopAll();
  imaging::hardReset();
  leds::allOff();
  io::configure(false);
  leds::reset(false);
  magnet::invalidateSetup();  // its control pins were just torn down
  // Let attached boards notice the heartbeat has stopped and power down.
  delay(1000);
  gSystemEnabled = false;
}

}  // namespace

void setEnabled(bool enable) {
  if (enable == gSystemEnabled) return;  // reconfiguring a live system would
                                         // glitch it; re-disabling wastes ~2 s
  if (enable) {
    io::configure(true);
    imaging::latchTriggerBaseline();
    if (gLedAttached) leds::reset(true);
    gSystemEnabled = true;
  } else {
    teardownToSafeState();
  }
}

void bootSafeState() { teardownToSafeState(); }

void resetConfig() {
  setEnabled(false);
  imaging::resetSequenceConfig();
  io::resetGpioConfig();
  signals::resetAll();
}

}  // namespace system_
