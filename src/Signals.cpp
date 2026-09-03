#include "Signals.h"

#include <cstring>

#include "Io.h"
#include "Leds.h"
#include "Magnet.h"
#include "Protocol.h"
#include "digitalWriteFast.h"

namespace signals {
namespace {

struct Signal {
  bool active = false;
  bool repeat = false;
  bool isSlave = false;
  Mode mode = Mode::kAdc;
  uint32_t option = 0;
  int gpioPin = -1;        // resolved Teensy pin for kGpioWrite
  int count = 0;           // configured steps
  int pos = -1;            // last executed step; -1 = not started
  uint32_t lastFireUs = 0;
  uint32_t intervalUs = 0; // delay until the next step
  float data[kMaxLength];
  int32_t delayUs[kMaxLength];  // -1 = end of sequence
};

Signal sigs[kNumSignals];

bool isRecordingMode(Mode m) {
  return m == Mode::kAdc || m == Mode::kMagAdc || m == Mode::kMagHallRead;
}

void rewind(Signal& s) {
  s.pos = -1;
  s.lastFireUs = 0;
  s.intervalUs = 0;
}

void stopAndRewind(int index) {
  Signal& s = sigs[index];
  s.active = false;
  rewind(s);
  if (s.mode == Mode::kConductor) {
    for (int i = 0; i < kNumSignals; i++) {
      if ((s.option & (1u << i)) && sigs[i].isSlave) {
        sigs[i].active = false;
        rewind(sigs[i]);
      }
    }
  }
}

void step(int index);

void performAction(int selfIndex, Signal& s, int idx) {
  const float value = s.data[idx];
  switch (s.mode) {
    case Mode::kAdc:
      s.data[idx] = io::readAdcVolts((int)s.option, 0);
      break;
    case Mode::kMagAdc:
      s.data[idx] = io::readAdcVolts((int)s.option, 2);
      break;
    case Mode::kMagHallRead:
      s.data[idx] = magnet::readHallMilliTesla((int)s.option);
      break;
    case Mode::kDac:
      io::setDacVolts((int)s.option, value);
      break;
    case Mode::kMagDac:
      magnet::setDacVolts((int)s.option, value);
      break;
    case Mode::kMagCurrent:
      magnet::setCurrent(s.option == 1, value);
      break;
    case Mode::kMagField:
      magnet::setField(s.option == 1, value);
      break;
    case Mode::kDo:
      io::writeDigitalOut((int)s.option, value > 0.0f);
      break;
    case Mode::kDoTimed:
      if (value > 0.0f) io::pulseDigitalOut((int)s.option, value);
      break;
    case Mode::kLed:
      leds::switchOn((int)s.option, value > 0.0f);
      break;
    case Mode::kLedTimed:
      if (value > 0.0f) leds::switchTimed((int)s.option, value);
      break;
    case Mode::kGpioWrite:
      digitalWriteFast(s.gpioPin, value > 0.0f ? HIGH : LOW);
      break;
    case Mode::kConductor:
      for (int i = 0; i < kNumSignals; i++) {
        if (i == selfIndex) continue;  // a conductor must never drive itself
        if ((s.option & (1u << i)) && sigs[i].active && sigs[i].isSlave) {
          step(i);
        }
      }
      break;
    case Mode::kGpioRead:
      protocol::fault("GPIO read signals are not implemented");
      break;
  }
}

// Executes the next step of a signal and schedules the one after.
void step(int index) {
  Signal& s = sigs[index];
  s.lastFireUs = micros();

  int idx = s.pos + 1;
  if (idx >= s.count || s.delayUs[idx] == -1) {
    if (s.repeat && idx > 1) {
      if (isRecordingMode(s.mode)) {
        // Rolling record: drop the oldest sample, append at the end.
        memmove(&s.data[0], &s.data[1], (idx - 1) * sizeof(float));
        idx = idx - 1;
      } else {
        idx = 0;  // replay the sequence
      }
    } else if (s.repeat && idx == 1) {
      idx = 0;  // single-step repeat: no rolling needed
    } else {
      stopAndRewind(index);
      return;
    }
  }

  performAction(index, s, idx);
  s.intervalUs = (uint32_t)s.delayUs[idx];
  s.pos = idx;
}

}  // namespace

bool validIndex(int index) {
  if (index < 0 || index >= kNumSignals) {
    protocol::fault("signal index %d out of range 0..%d", index, kNumSignals - 1);
    return false;
  }
  return true;
}

void configure(int index, Mode mode, uint32_t option, bool repeat, bool isSlave) {
  if (!validIndex(index)) return;
  Signal& s = sigs[index];
  if (s.active) {
    protocol::fault("signal %d is active; stop it before reconfiguring", index);
    return;
  }
  if ((uint8_t)mode > (uint8_t)Mode::kGpioWrite || mode == Mode::kGpioRead) {
    protocol::fault("unsupported signal mode %d", (int)mode);
    return;
  }
  if (mode == Mode::kDo || mode == Mode::kDoTimed) {
    if (option < 1 || option > 4) {
      protocol::fault("digital output option %lu out of range 1..4", (unsigned long)option);
      return;
    }
  }
  if (mode == Mode::kGpioWrite) {
    s.gpioPin = io::claimGpioAsOutput((int)option);
    if (s.gpioPin < 0) return;
  }
  s.mode = mode;
  s.option = option;
  s.repeat = repeat;
  s.isSlave = isSlave;
}

void load(int index, int count, const float* values, const float* delaysMs) {
  if (!validIndex(index)) return;
  Signal& s = sigs[index];
  if (s.active) {
    protocol::fault("signal %d is active; stop it before loading data", index);
    return;
  }
  if (count < 1 || count > kMaxLength) {
    protocol::fault("signal length %d out of range 1..%d", count, kMaxLength);
    return;
  }
  bool dacRateWarned = false;
  for (int i = 0; i < count; i++) {
    s.data[i] = values[i];
    if (s.isSlave) {
      s.delayUs[i] = 0;  // timing comes from the conductor
      continue;
    }
    const float delayMs = delaysMs[i];
    const int32_t delayUs = (int32_t)(delayMs * 1000.0f);
    if ((delayUs <= 0 && delayUs != -1) || (delayUs == -1 && i == 0)) {
      protocol::fault("signal %d step %d: delay %.3f ms is invalid (must be positive, or -1 to end)",
                      index, i, (double)delayMs);
      return;
    }
    if (delayUs != -1 && delayUs < 10 && s.mode == Mode::kDac && !dacRateWarned) {
      protocol::logf("WARNING: signal %d asks for DAC updates faster than ~100 kHz; "
                     "the DAC and its output amplifier cannot follow accurately",
                     index);
      dacRateWarned = true;
    }
    s.delayUs[i] = delayUs;
  }
  s.count = count;
  rewind(s);
}

void loadUniform(int index, int count, float intervalMs) {
  if (!validIndex(index)) return;
  Signal& s = sigs[index];
  if (s.active) {
    protocol::fault("signal %d is active; stop it before loading data", index);
    return;
  }
  if (count < 1 || count > kMaxLength) {
    protocol::fault("signal length %d out of range 1..%d", count, kMaxLength);
    return;
  }
  const int32_t delayUs = (int32_t)(intervalMs * 1000.0f);
  if (delayUs <= 0) {
    protocol::fault("interval %.3f ms is invalid", (double)intervalMs);
    return;
  }
  if (isRecordingMode(s.mode) && delayUs < 1000) {
    protocol::logf("WARNING: one ADC read takes ~0.6 ms; a %.3f ms sampling interval "
                   "will not be honoured",
                   (double)intervalMs);
  }
  for (int i = 0; i < count; i++) {
    s.data[i] = 0.0f;
    s.delayUs[i] = s.isSlave ? 0 : delayUs;
  }
  s.count = count;
  rewind(s);
}

void start(int index) {
  if (!validIndex(index)) return;
  Signal& s = sigs[index];
  if (s.active) {
    protocol::fault("signal %d is already active", index);
    return;
  }
  if (s.count == 0) {
    protocol::fault("signal %d has no data loaded", index);
    return;
  }
  rewind(s);
  if (isRecordingMode(s.mode)) memset(s.data, 0, s.count * sizeof(float));
  s.active = true;

  if (s.mode == Mode::kConductor) {
    for (int i = 0; i < kNumSignals; i++) {
      if ((s.option & (1u << i)) && sigs[i].isSlave) {
        // Rewind slaves too, or their patterns come out rotated relative
        // to each other after an aborted run.
        rewind(sigs[i]);
        if (isRecordingMode(sigs[i].mode)) memset(sigs[i].data, 0, sigs[i].count * sizeof(float));
        sigs[i].active = true;
      }
    }
  }
}

void stop(int index) {
  if (!validIndex(index)) return;
  stopAndRewind(index);
}

void stopAll() {
  for (int i = 0; i < kNumSignals; i++) {
    sigs[i].active = false;
    rewind(sigs[i]);
  }
}

void resetAll() {
  stopAll();
  for (int i = 0; i < kNumSignals; i++) {
    sigs[i].count = 0;
    sigs[i].repeat = false;
    sigs[i].isSlave = false;
    sigs[i].mode = Mode::kAdc;
    sigs[i].option = 0;
  }
}

bool isActive(int index) { return sigs[index].active; }

int length(int index) { return sigs[index].count; }

const float* data(int index) { return sigs[index].data; }

void tick() {
  const uint32_t now = micros();
  for (int i = 0; i < kNumSignals; i++) {
    Signal& s = sigs[i];
    if (s.active && !s.isSlave && (uint32_t)(now - s.lastFireUs) >= s.intervalUs) {
      step(i);
    }
  }
}

}  // namespace signals
