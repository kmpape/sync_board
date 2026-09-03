#pragma once
#include <Arduino.h>

// Camera-synchronised image sequences: up to kMaxImages back-to-back frames,
// each optionally lighting one LED.
//
// Sync modes:
//   0  triggers ignored (default)
//   1  the host starts a sequence over serial; we trigger the camera
//   2  an external rising edge on the camera-LED1 line starts the sequence
//
// Independently, LED timing per frame comes either from the camera's
// per-LED gating outputs (ledByCamera = true) or from our own exposure
// timers. A non-zero exposure time always acts as a timer.

namespace imaging {

constexpr int kMaxImages = 4;

void setSyncMode(int mode, bool ledByCamera);  // faults while a sequence runs
int syncMode();

// Configures the sequence: for each frame, whether it is active, which LED
// channel lights it (0 = none), and its exposure time. Active frames must be
// contiguous from frame 0. Faults (and deactivates everything) on invalid
// input.
void setupSequence(const bool active[kMaxImages], const int led[kMaxImages],
                   const uint32_t exposureUs[kMaxImages]);

// Starts a sequence over serial (sync mode 1 only). expectedImages must
// match the configured number of active frames.
void startSequence(int expectedImages);

// Aborts any sequence and forces camera trigger + state to idle. Does not
// touch LEDs; callers turn those off through the LED driver.
void hardReset();

// Scans trigger inputs and services timers; call every loop iteration while
// the system is enabled.
void tick();

// Re-reads the trigger pins as the edge-detection baseline; call right after
// the pins have been configured (system enable).
void latchTriggerBaseline();

// Clears the configured sequence (all frames inactive). Sync mode is kept.
void resetSequenceConfig();

}  // namespace imaging
