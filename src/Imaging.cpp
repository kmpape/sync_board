#include "Imaging.h"

#include "Leds.h"
#include "Pins.h"
#include "Protocol.h"
#include "State.h"
#include "System.h"
#include "digitalWriteFast.h"

namespace imaging {
namespace {

// Camera trigger pulse width; just long enough for the camera to see it.
constexpr uint32_t kCameraTriggerUs = 1000;
// A whole sequence must finish within this or something is wrong.
constexpr uint32_t kSequenceTimeoutUs = 10000000;

int mode = 0;
bool ledByCamera = false;

bool imgActive[kMaxImages] = {false};
int imgLed[kMaxImages] = {0};
uint32_t imgExposureUs[kMaxImages] = {0};

int currentImage = -1;   // -1 = idle; >= 0 means a sequence is in flight
bool exposing = false;
uint32_t exposureStartUs = 0;
uint32_t sequenceStartUs = 0;

bool cameraTriggerHigh = false;
uint32_t cameraTriggerStartUs = 0;

// Trigger inputs we watch for edges. Index 0..3 are the camera's per-LED
// gating lines; index 4 is the external sequence-start line for sync mode 2
// (shared with the LED1 line on this board revision).
constexpr int kNumTriggers = 5;
const int kTriggerPins[kNumTriggers] = {
    pins::kCameraLed[0], pins::kCameraLed[1], pins::kCameraLed[2],
    pins::kCameraLed[3], pins::kCameraLed[0]};
int prevTriggerState[kNumTriggers];

bool running() { return currentImage >= 0; }

void setFrameLed(int image, bool on) {
  const int led = imgLed[image];
  if (led == 0) return;
  if (!gLedAttached) {
    protocol::fault("frame %d wants LED %d but no LED board is attached", image, led);
    return;
  }
  leds::switchOn(led, on, /*force=*/true);
}

void endSequence() {
  currentImage = -1;
  protocol::logf("image sequence finished in %lu us",
                 (unsigned long)(micros() - sequenceStartUs));
}

// Starts exposing the next frame.
void startImage() {
  if (exposing) {
    protocol::fault("new frame requested while still exposing; timings/triggers overlap");
    return;
  }
  currentImage++;

  if (!ledByCamera) setFrameLed(currentImage, true);

  if (mode == 1) {
    if (!digitalReadFast(pins::kCameraTriggerReady)) {
      protocol::fault("camera triggered before it reported ready");
    }
    if (cameraTriggerHigh) {
      // Previous frame's pulse still high (very short frame): give the
      // camera a clean falling edge before re-triggering.
      digitalWriteFast(pins::kCameraTriggerIn, LOW);
      delayMicroseconds(5);
    }
    digitalWriteFast(pins::kCameraTriggerIn, HIGH);
    cameraTriggerHigh = true;
    cameraTriggerStartUs = micros();
  }

  exposureStartUs = micros();
  exposing = true;
}

void endImage() {
  setFrameLed(currentImage, false);
  if (!exposing) {
    protocol::fault("frame end requested while not exposing");
    return;
  }
  exposing = false;
}

// Ends the current frame and either starts the next one or the sequence.
void finishImage() {
  endImage();
  const int next = currentImage + 1;
  if (next >= kMaxImages || !imgActive[next]) {
    endSequence();
  } else {
    startImage();
  }
}

// A camera per-LED gating edge arrived (only relevant with ledByCamera).
void handleLedTrigger(int ledIndex, bool high) {
  if (!ledByCamera || !running()) return;
  if (ledIndex != currentImage) {
    protocol::fault("camera gated frame %d but we are on frame %d; frames overlap",
                    ledIndex, currentImage);
    return;
  }
  if (high) {
    if (!exposing) {
      protocol::fault("camera gated LED on before the frame started");
      return;
    }
    setFrameLed(ledIndex, true);
  } else {
    finishImage();
  }
}

void beginSequence() {
  if (running()) {
    protocol::fault("sequence start requested while one is already running");
    return;
  }
  sequenceStartUs = micros();
  startImage();
}

}  // namespace

void setSyncMode(int newMode, bool newLedByCamera) {
  if (running()) {
    protocol::fault("cannot change sync mode while a sequence is running");
    return;
  }
  if (newMode < 0 || newMode > 2) {
    protocol::fault("sync mode %d out of range 0..2", newMode);
    return;
  }
  mode = newMode;
  ledByCamera = newLedByCamera;
}

int syncMode() { return mode; }

void setupSequence(const bool active[kMaxImages], const int led[kMaxImages],
                   const uint32_t exposureUs[kMaxImages]) {
  if (running()) {
    protocol::fault("cannot change the image sequence while it is running");
    return;
  }
  bool seenInactive = false;
  for (int i = 0; i < kMaxImages; i++) {
    if (!active[i]) {
      seenInactive = true;
    } else if (seenInactive) {
      protocol::fault("active frames must be contiguous from frame 0");
      return;
    }
    if (led[i] < 0 || led[i] > 8) {
      protocol::fault("frame %d LED choice %d out of range 0..8", i, led[i]);
      return;
    }
    if (exposureUs[i] > 2000000) {
      protocol::fault("frame %d exposure exceeds the 2 s maximum", i);
      return;
    }
    if (ledByCamera && exposureUs[i] > 0) {
      protocol::fault("frame %d has a fixed exposure time but LED gating is set to "
                      "camera control; set exposures to 0 or change the sync mode",
                      i);
      return;
    }
    if (!ledByCamera && active[i] && exposureUs[i] == 0) {
      protocol::fault("frame %d has no exposure time; without camera-gated LEDs the "
                      "frame would end instantly",
                      i);
      return;
    }
  }
  for (int i = 0; i < kMaxImages; i++) {
    imgActive[i] = active[i];
    imgLed[i] = led[i];
    imgExposureUs[i] = exposureUs[i];
  }
}

void startSequence(int expectedImages) {
  if (exposing) {
    protocol::fault("cannot start a sequence while exposing");
    return;
  }
  if (mode != 1) {
    protocol::fault("serial sequence start requires sync mode 1");
    return;
  }
  int numActive = 0;
  while (numActive < kMaxImages && imgActive[numActive]) numActive++;
  if (numActive == 0) {
    protocol::fault("no active frames configured");
    return;
  }
  if (numActive != expectedImages) {
    protocol::fault("host expects %d frames but %d are configured", expectedImages, numActive);
    return;
  }
  for (int i = 0; i < numActive; i++) {
    // Re-check here: the sync mode may have changed since setupSequence.
    if (!ledByCamera && imgExposureUs[i] == 0) {
      protocol::fault("frame %d has no exposure time; without camera-gated LEDs the "
                      "frame would end instantly",
                      i);
      return;
    }
  }
  beginSequence();
}

bool sequenceRunning() { return running(); }

void hardReset() {
  currentImage = -1;
  exposing = false;
  digitalWriteFast(pins::kCameraTriggerIn, LOW);
  cameraTriggerHigh = false;
}

void tick() {
  // Release the camera trigger pulse independently of sequence state.
  if (cameraTriggerHigh &&
      (uint32_t)(micros() - cameraTriggerStartUs) >= kCameraTriggerUs) {
    digitalWriteFast(pins::kCameraTriggerIn, LOW);
    cameraTriggerHigh = false;
  }

  // Edge-detect the trigger inputs, scanned from the highest index down:
  // in sync mode 2 the start line shares a pin with the frame-0 LED gate,
  // and the sequence must begin (index 4) before that same rising edge is
  // interpreted as the frame-0 gate (index 0). The baseline is tracked even
  // in sync mode 0, so switching modes does not turn an old level change
  // into a spurious fresh edge.
  for (int i = kNumTriggers - 1; i >= 0; i--) {
    const int state = digitalReadFast(kTriggerPins[i]);
    if (state != prevTriggerState[i] && mode != 0) {
      if (i < 4) {
        handleLedTrigger(i, state);
      } else if (mode == 2 && state) {
        beginSequence();
      }
    }
    prevTriggerState[i] = state;
  }
  if (mode == 0) return;

  if (!running()) return;

  // Timer-based exposure end (also overrides camera gating when a frame has
  // an explicit exposure time).
  if (exposing && (!ledByCamera || imgExposureUs[currentImage] > 0)) {
    if ((uint32_t)(micros() - exposureStartUs) >= imgExposureUs[currentImage]) {
      finishImage();
    }
  }

  if (running() && (uint32_t)(micros() - sequenceStartUs) > kSequenceTimeoutUs) {
    protocol::logf("ERROR: image sequence timed out; disabling the system as a precaution");
    system_::setEnabled(false);  // also hard-resets imaging and turns LEDs off
  }
}

void latchTriggerBaseline() {
  for (int i = 0; i < kNumTriggers; i++) {
    prevTriggerState[i] = digitalReadFast(kTriggerPins[i]);
  }
}

void resetSequenceConfig() {
  for (int i = 0; i < kMaxImages; i++) {
    imgActive[i] = false;
    imgLed[i] = 0;
    imgExposureUs[i] = 0;
  }
}

}  // namespace imaging
