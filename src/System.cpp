#include "System.h"

#include <Arduino.h>

#include "Imaging.h"
#include "Io.h"
#include "Leds.h"
#include "Signals.h"
#include "State.h"

bool gSystemEnabled = false;
bool gLedAttached = false;
bool gMagnetAttached = false;

namespace system_ {

void setEnabled(bool enable) {
  if (enable) {
    io::configure(true);
    imaging::latchTriggerBaseline();
    if (gLedAttached) leds::reset(true);
    gSystemEnabled = true;
    return;
  }

  // Stop all activity first so nothing re-drives an output mid-shutdown.
  signals::stopAll();
  imaging::hardReset();
  leds::allOff();
  io::configure(false);
  leds::reset(false);
  // Let attached boards notice the heartbeat has stopped and power down.
  delay(1000);
  gSystemEnabled = false;
}

void resetConfig() {
  setEnabled(false);
  imaging::resetSequenceConfig();
  io::resetGpioConfig();
  signals::resetAll();
}

}  // namespace system_
