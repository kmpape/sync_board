#!/usr/bin/env python3
"""First light for the screener board: turns on the five indicator LEDs in
the column along its top edge (by the SyncBoard connector), holds them until
you press Enter, then switches everything off.

    LL-5  12 V input      lit whenever H2 is powered (not software controlled)
    LL-6  ENWD            the isolated heartbeat: lights once the SyncBoard is enabled
    LL-4  SensorsEnable   PCA9685 channel 12
    LL-3  FluxgateEnable  PCA9685 channel 11
    LL-2  RF_Enable       PCA9685 channel 1

LL-2..LL-4 sit on the enable lines themselves, so lighting them also switches
on what those lines enable: the flex-sensor and LED-display rails (and the
RTD muxes), the fluxgate supply, and RF detectors 1-3.

Only the I2C side is needed (the PCA9685); the SPI ADC may be absent.

Examples:
    python hwtest/screener_leds.py
    python hwtest/screener_leds.py --port /dev/cu.usbmodem1234
"""

from __future__ import annotations

import argparse
import logging
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))

from syncboard import CommandError, ScreenerEnable, SyncBoard, SyncBoardError  # noqa: E402

# (indicator, enable) in the order they sit, top to bottom, below LL-5/LL-6.
LEDS = [
    ("LL-4", ScreenerEnable.SENSORS),
    ("LL-3", ScreenerEnable.FLUXGATE),
    ("LL-2", ScreenerEnable.RF),
]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", help="serial port (default: auto-discover)")
    args = parser.parse_args()

    logging.basicConfig(level=logging.INFO, format="  [%(levelname)s] %(message)s")

    print("Connecting...")
    try:
        board = SyncBoard.connect(args.port)
    except SyncBoardError as exc:
        print(f"Could not connect: {exc}")
        return 2
    try:
        try:
            board.disable()  # attaching needs a disabled system
            board.attach_screener_board(True)
        except CommandError as exc:
            print(f"The firmware does not know the screener board ({exc.message}); "
                  "flash it with 'pio run -t upload'.")
            return 1
        board.enable()  # starts the heartbeat: LL-6 should light now
        chips = board.screener.setup()
        # Each LED on its own, so one failure does not hide the others.
        lit = []
        if chips.pca9685:
            for name, enable in LEDS:
                try:
                    board.screener.set_enable(enable, True)
                    lit.append(name)
                except SyncBoardError as exc:
                    print(f"  {name}: {exc}")
        else:
            print("\nThe PCA9685 (0x51) did not answer, so LL-2..LL-4 cannot be driven. Is "
                  "the board powered (12 V on H2)? LL-5 and LL-6 still show power and heartbeat.")
        print("\nExpect lit, top to bottom: LL-5 (12 V), LL-6 (heartbeat)"
              + "".join(f", {name}" for name in lit) + ".")
        print("  LL-6 dark: the heartbeat is not reaching the board. It crosses on Iso-2,")
        print("  the same isolator as the SPI lines, and while it is down the PCA9685's")
        print("  outputs are held off too (PWM1 pin 23 at ~3.3 V), so LL-2..LL-4 stay dark.")
        if lit:
            print("  LL-6 lit but LL-2..LL-4 dark: probe PWM1 pins 6, 17 and 18 (~3.3 V when on).")
        input("\nPress Enter to switch them off and quit... ")
    except KeyboardInterrupt:
        print()
    finally:
        board.close()  # disables the system: the heartbeat stops and every output drops
    return 0


if __name__ == "__main__":
    sys.exit(main())
