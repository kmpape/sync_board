#pragma once

// Global system state shared between modules. Kept deliberately small: only
// the flags that genuinely cut across subsystems live here.

// True while the system is enabled (buses up, IO configured, heartbeat
// running). Only System::setEnabled() may change it.
extern bool gSystemEnabled;

// True when the operator has declared the LED / magnet / screener expansion
// boards present (attachLed / attachMagnet / attachScreener commands).
extern bool gLedAttached;
extern bool gMagnetAttached;
extern bool gScreenerAttached;

constexpr const char* kFirmwareVersion = "2.2.0";
