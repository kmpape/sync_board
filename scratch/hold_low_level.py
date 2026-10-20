"""Holds LED channel 5 on at a low level so the channel can be probed.

Enables the system (LED rails up), then runs channel 5 at LEVEL in current
mode for up to 20 s while printing the measured current. Press Ctrl-C to
stop early; the system is disabled on exit either way.

Run from the repo root:  .venv/bin/python scratch/hold_low_level.py [level]
"""

import sys
import time

from syncboard import FeedbackMode, SyncBoard

CH = 5
LEVEL = float(sys.argv[1]) if len(sys.argv) > 1 else 0.02   # ~0.14 A
assert LEVEL <= 0.05, "keep this a low-power probe"
HOLD_S = 20

sb = SyncBoard.connect()
try:
    sb.initialise(led_board=True)
    m = sb.leds.measure(CH)
    print(f"enabled, LED off: {m.current_a * 1000:.1f} mA. "
          f"Probe VLedA (CE-2) now if you like; pulse starts in 5 s.")
    time.sleep(5)
    sb.leds.set_level(CH, LEVEL, FeedbackMode.CURRENT)
    sb.leds.pulse(CH, HOLD_S * 1000)
    t0 = time.monotonic()
    while time.monotonic() - t0 < HOLD_S:
        m = sb.leds.measure(CH)
        print(f"t={time.monotonic() - t0:4.1f}s  current {m.current_a * 1000:7.1f} mA  "
              f"photodiode {sb.leds.measure_photodiode(CH):7.1f} mV")
        if m.current_a > LEVEL * 7.2 * 1.5 + 0.1:
            print("ABORT: current above setpoint")
            break
        time.sleep(1.0)
except KeyboardInterrupt:
    pass
finally:
    sb.close()      # disables: LED off, rails off
    print("disabled")
