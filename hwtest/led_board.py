#!/usr/bin/env python3
"""Interactive checkout for the LED board, driven through a SyncBoard.

Covers the parts of docs/bringup/led-board.md that need a SyncBoard: the
heartbeat interlock, the LED rails, the I2C chips, the per-channel enables,
and (optionally) one channel's current loop into a dummy load. Run the
standalone rail checks from the doc first.

Examples:
    python hwtest/led_board.py                          # link, heartbeat, i2c, enables
    python hwtest/led_board.py --sections heartbeat     # just one section
    python hwtest/led_board.py --channels 5             # only channel 5 is populated
    python hwtest/led_board.py --channel 5 --max-current 2
                                                        # ...plus the current loop on ch 5

Before running:
  - Supply limit at 1-2 A. The LED rail bricks start when the heartbeat
    comes up and brown out at 0.2 A.
  - 'current' drives real current: a power resistor on the channel, not a
    LED, and --max-current well inside what it can dissipate.
"""

from __future__ import annotations

import argparse
import logging
import pathlib
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))

from checkout import Checkout  # noqa: E402
from syncboard import SyncBoard, SyncBoardError  # noqa: E402

LED_CHIPS = {0x49: "ADS7828 ADC", 0x50: "PCA9685 PWM", 0x57: "AD5669 DAC"}


class LedBoardCheckout(Checkout):

    def section_link(self) -> None:
        # Attached but not enabled: the SyncBoard powers the isolator input
        # side but HB stays low, so the whole board must be forced off.
        self.banner("Link (attached, not enabled)")
        self.board.disable()
        self.board.attach_led_board(True)
        self.confirm("link power",
                     "LED-8 (5V_IN) lit, LED7 (HB), LED1-4 (VLedA) and LED1-3 (VLedB) all off?")
        self.confirm("forced off",
                     "HB reads 0 V, VLedA/VLedB read 0 V, and every MOSFET gate (M1 pin 1) "
                     "reads 0 V?")

    def section_heartbeat(self) -> None:
        self.banner("Heartbeat release and LED rails")
        self.board.enable()
        self.confirm("heartbeat up",
                     "LED7 lit and HB reads ~3.3 V? (PWM1 pin 23 should now be 0 V)")
        self.confirm("LED rails up",
                     "LED1-4 / LED1-3 lit, VLedA and VLedB at the voltages you expect "
                     "from the trim resistors (about 6 V / 9 V as designed)?")
        self.confirm("channels still off",
                     "All eight channel indicators (LEDC_1) off and gates still 0 V?")
        print("  Disabling: the heartbeat stops and the watchdog should drop HB...")
        self.board.disable()
        self.confirm("interlock",
                     "LED7 off, HB back to 0 V, and both LED rails collapsed?")
        self.board.enable()

    def section_i2c(self) -> None:
        self.banner("I2C chips")
        addresses = self.board.scan_i2c()
        print(f"  I2C devices: {[hex(a) for a in addresses]}")
        for address, name in LED_CHIPS.items():
            self.auto(f"{name} at {hex(address)}", address in addresses)

    def section_enables(self, channels: list[int]) -> None:
        # With no level set the DAC sits at 0 V, so the enable line lights the
        # channel's Active indicator without any current flowing. Walking the
        # channels exercises the Teensy pins, the isolators, the AND gates and
        # the D-latches.
        names = ", ".join(str(c) for c in channels)
        self.banner(f"Per-channel enables (no current), channels {names}")
        print("  Watch the channel indicator(s) (LEDC_1). Each lights in turn")
        print(f"  for a second: channel {names}.")
        self.instruct("Ready to watch the indicators")
        problems = []
        for ch in channels:
            try:
                self.board.leds.pulse(ch, 1000)
            except SyncBoardError as exc:
                problems.append(f"ch{ch}: {exc}")
            time.sleep(1.2)
        self.auto("enable commands accepted", not problems, "; ".join(problems))
        self.confirm("indicators walk",
                     f"Did the indicator(s) light one at a time, in order {names}, "
                     "and nothing else light?")

        # Readback path: the DB mux (PCA9685) plus the ADS7828 answer on each
        # channel. The value is meaningless without a photodiode; only the fact
        # that it reads matters here.
        readings = []
        try:
            for ch in channels:
                readings.append(self.board.leds.measure_photodiode(ch))
            self.auto("ADC readback", True,
                      ", ".join(f"ch{c} {v:.0f}" for c, v in zip(channels, readings))
                      + " mV (uncalibrated)")
        except SyncBoardError as exc:
            self.auto("ADC readback", False, str(exc))

    def section_current(self, channel: int, max_current_a: float) -> None:
        self.banner(f"Current loop (channel {channel}, dummy load, max {max_current_a} A)")
        self.instruct(f"Power resistor on channel {channel}'s LED connector, "
                      f"ammeter in series or meter across it, fans on")
        print("  Calibrating: sweeps the current up to the max, takes a few seconds...")
        try:
            self.board.leds.calibrate(channel, max_current_a)
        except SyncBoardError as exc:
            self.auto("calibration", False, str(exc))
            return
        self.auto("calibration", True)

        # 20% is under the 30% untimed cap, so the channel can stay on while
        # the operator reads the meter.
        self.board.leds.set_level(channel, 0.2)
        self.board.leds.on(channel)
        try:
            m = self.board.leds.measure(channel)
            expected = 0.2 * 0.95 * max_current_a
            print(f"  Board reports {m.current_a:.3f} A (expect about {expected:.2f} A); "
                  f"sense pin = VL + 0.15 V/A x I = {0.3 + 0.15 * m.current_a:.3f} V")
            raw = input("  Meter reading in A (blank to skip): ").strip()
            if raw:
                meter = float(raw)
                self.auto("board vs meter", abs(meter - m.current_a) <= 0.1 * max(meter, 0.2),
                          f"board {m.current_a:.3f} A, meter {meter:.3f} A")
            else:
                self.results.append(("board vs meter", "SKIP"))
        except SyncBoardError as exc:
            self.auto("measure while on", False, str(exc))
        finally:
            self.board.leds.off(channel)
            self.board.leds.set_level(channel, 0.0)


SECTIONS = ["link", "heartbeat", "i2c", "enables", "current"]
DEFAULT_SECTIONS = ["link", "heartbeat", "i2c", "enables"]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", help="serial port (default: auto-discover)")
    parser.add_argument("--sections", help=f"comma-separated subset of: {','.join(SECTIONS)}")
    parser.add_argument("--channels", default="1,2,3,4,5,6,7,8", metavar="LIST",
                        help="populated channels for the 'enables' section, e.g. 5 or 1,2,5 "
                             "(default: all eight)")
    parser.add_argument("--channel", type=int, metavar="CH",
                        help="run the 'current' section on this channel (1-8)")
    parser.add_argument("--max-current", type=float, metavar="A",
                        help="max current for the 'current' section's calibration sweep")
    args = parser.parse_args()

    try:
        channels = [int(c) for c in args.channels.split(",")]
    except ValueError:
        parser.error("--channels must be a comma-separated list of numbers")
    if not channels or any(c < 1 or c > 8 for c in channels):
        parser.error("--channels entries must be in 1..8")

    sections = args.sections.split(",") if args.sections else list(DEFAULT_SECTIONS)
    if args.channel is not None and "current" not in sections:
        sections.append("current")
    unknown = set(sections) - set(SECTIONS)
    if unknown:
        parser.error(f"unknown sections: {', '.join(sorted(unknown))}")
    if "current" in sections and (args.channel is None or args.max_current is None):
        parser.error("the current section needs --channel CH and --max-current A")

    logging.basicConfig(level=logging.INFO, format="  [%(levelname)s] %(message)s")

    print("Connecting...")
    try:
        board = SyncBoard.connect(args.port)
    except SyncBoardError as exc:
        print(f"Could not connect: {exc}")
        return 2
    checkout = LedBoardCheckout(board)
    try:
        if "link" not in sections:
            # Later sections assume the board is attached and enabled.
            board.initialise(led_board=True)
        for name in sections:
            if name == "current":
                checkout.section_current(args.channel, args.max_current)
            elif name == "enables":
                checkout.section_enables(channels)
            else:
                getattr(checkout, f"section_{name}")()
    except KeyboardInterrupt:
        print("\nInterrupted; disabling the board.")
    finally:
        board.close()  # disables the system on the way out

    print("\n" + "=" * 64)
    width = max((len(step) for step, _ in checkout.results), default=0)
    for step, result in checkout.results:
        print(f"  {step:<{width}}  {result}")
    failed = [step for step, result in checkout.results if result == "FAIL"]
    print(f"\n{len(checkout.results)} checks: "
          f"{sum(1 for _, r in checkout.results if r == 'PASS')} passed, "
          f"{len(failed)} failed, "
          f"{sum(1 for _, r in checkout.results if r == 'SKIP')} skipped")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
