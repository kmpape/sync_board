#pragma once

// System-level orchestration: the enable/disable sequence and soft resets.
// (Named system_ because <cstdlib> owns ::system.)

namespace system_ {

// Enable: brings up buses and IO, applies the stored GPIO config, resets the
// LED driver, and starts trigger detection. Disable: stops all activity,
// puts every output into a safe state, and powers down expansion chips.
// Both are no-ops when already in the requested state. Disable blocks for
// ~1 s so attached boards see the heartbeat stop.
void setEnabled(bool enable);

// The unconditional safe-state pass run once from setup(): pin states after
// reset/reflash are unknown, so this always performs the full teardown.
void bootSafeState();

// Soft reset of all configuration (GPIOs, signals, image sequence, LED
// levels) — equivalent to a power cycle, minus the EEPROM. Leaves the
// system disabled.
void resetConfig();

}  // namespace system_
