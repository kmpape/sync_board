#!/usr/bin/env python3
"""Interactive hardware checkout for the SyncBoard.

Run this sitting at the bench with an oscilloscope/voltmeter. It walks
through each subsystem, drives real outputs, and asks you to confirm what
you measure. At the end it prints a pass/fail summary.

Examples:
    python hwtest/checkout.py                        # core sections
    python hwtest/checkout.py --sections do,dac      # just these
    python hwtest/checkout.py --led 1 --magnet       # include expansion boards
    python hwtest/checkout.py --list                 # show available sections

Section notes:
  - 'gpio' needs a jumper wire (loops one GPIO output into a DI input).
  - 'led' powers the selected LED channel: make sure it can safely light.
  - 'magnet' drives real current through the coil during calibration.
  - 'imaging' pulses the camera trigger line; a camera need not be attached.
"""

from __future__ import annotations

import argparse
import logging
import pathlib
import sys
import threading
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))

from syncboard import Frame, SignalMode, SyncBoard, SyncBoardError  # noqa: E402  (SyncBoardError also catches CommandError)


class Checkout:
    def __init__(self, board: SyncBoard):
        self.board = board
        self.results: list[tuple[str, str]] = []  # (step, PASS/FAIL/SKIP)

    # -- operator interaction ------------------------------------------------

    def confirm(self, step: str, question: str) -> None:
        """Asks the operator to verify a measurement."""
        while True:
            answer = input(f"  {question} [y]es / [n]o / [s]kip: ").strip().lower()
            if answer in ("y", "n", "s"):
                break
        self.results.append((step, {"y": "PASS", "n": "FAIL", "s": "SKIP"}[answer]))

    def auto(self, step: str, ok: bool, detail: str = "") -> None:
        """Records a check the script verified by itself."""
        print(f"  {'PASS' if ok else 'FAIL'}: {step}{f' ({detail})' if detail else ''}")
        self.results.append((step, "PASS" if ok else "FAIL"))

    @staticmethod
    def instruct(text: str) -> None:
        input(f"  -> {text}  (press Enter when ready) ")

    @staticmethod
    def banner(title: str) -> None:
        print(f"\n=== {title} " + "=" * max(0, 60 - len(title)))

    # -- sections ------------------------------------------------------------

    def section_system(self) -> None:
        self.banner("System")
        version = self.board.ping()
        self.auto("ping", True, f"firmware {version}")
        addresses = self.board.scan_i2c()
        print(f"  I2C devices: {[hex(a) for a in addresses]}")
        # 0x40/0x60: PWM chips, 0x48: ADC - always on the SyncBoard itself.
        self.auto("i2c scan finds SyncBoard chips",
                  all(a in addresses for a in (0x40, 0x48, 0x60)))
        self.confirm("heartbeat",
                     "Scope the heartbeat (Teensy pin 41 / expansion header): "
                     "5 Hz square wave?")

    def section_do(self) -> None:
        # The connector-side DO nets (d4*-d7* on the schematic) go through
        # buffers supplied from the 5 V rail, so a high reads 5 V, not 3.3 V.
        self.banner("Digital outputs (D_OUT 1-4, buffered to 5 V)")
        for ch in range(1, 5):
            self.board.io.write_do(ch, True)
            self.confirm(f"DO{ch} high", f"D_OUT_{ch} reads ~5 V?")
            self.board.io.write_do(ch, False)
            self.confirm(f"DO{ch} low", f"D_OUT_{ch} reads ~0 V?")
        # Pulse until the operator answers, so there is no rush to arm the
        # scope; each pulse is individually firmware-timed via pulse_do.
        print("  Pulsing D_OUT_1 (50 ms high at 10 Hz) until you answer...")
        stop = threading.Event()

        def pump():
            while not stop.is_set():
                try:
                    self.board.io.pulse_do(1, 50)
                except SyncBoardError as exc:
                    print(f"  pulse train stopped: {exc}")
                    return
                time.sleep(0.1)

        pump_thread = threading.Thread(target=pump, daemon=True)
        pump_thread.start()
        try:
            self.confirm("DO1 pulses", "Scope D_OUT_1: repeating 50 ms high pulses?")
        finally:
            stop.set()
            pump_thread.join()

    def section_di(self) -> None:
        # D_OUT_1 (proven in the 'do' section) is the stimulus: it drives the
        # board's own 5 V logic level, and toggling it tests both input
        # states with a single jumper position per channel.
        self.banner("Digital inputs (D_IN 1-4, driven from D_OUT_1)")
        print("  Run the 'do' section first: this uses D_OUT_1 as the 5 V source.")
        for ch in range(1, 5):
            self.instruct(f"Jumper D_OUT_1 to D_IN_{ch}")
            self.board.io.write_do(1, True)
            high = self.board.io.read_di(ch)
            self.board.io.write_do(1, False)
            low = self.board.io.read_di(ch)
            self.auto(f"DI{ch} reads", high and not low,
                      f"high={high}, low={low}")

    def section_dac(self) -> None:
        self.banner("DAC outputs (AD5668, channels 1-8)")
        print("  Probe each channel's labelled DAC_n SMA connector (centre pin")
        print("  vs shield).")
        self.instruct("Put jumpers S1-16 through S1-19 (the DAC SMA routing "
                      "jumpers) into the S position (pins 1-2)")
        # Channel 1 gets a three-point sweep (checks the DAC + reference);
        # the rest get a single mid-scale check (checks each output's path).
        for volts in (0.5, 1.65, 3.0):
            self.board.io.set_dac(1, volts)
            self.confirm(f"DAC_1 = {volts} V", f"The DAC_1 SMA reads ~{volts} V?")
        self.board.io.set_dac(1, 0.0)
        for ch in range(2, 9):
            self.board.io.set_dac(ch, 2.0)
            self.confirm(f"DAC_{ch} = 2.0 V", f"The DAC_{ch} SMA reads ~2.0 V?")
            self.board.io.set_dac(ch, 0.0)

    def section_adc(self) -> None:
        self.banner("ADC inputs (ADS7828, channels 1-8)")
        # DAC-to-ADC loopback: automatic and absolute-checked, but ONLY valid
        # after the 'dac' section passed against a voltmeter. The DAC and ADC
        # share the Ref1 reference, so a wrong reference cancels out in this
        # loopback and would go undetected here.
        print("  Each channel is looped back from its DAC: run (and pass) the")
        print("  'dac' section first, or these results mean nothing.")
        print("  Channels 1-4 need their ADC SMA routing jumpers in S position.")
        for ch in range(1, 9):
            self.instruct(f"Connect DAC_{ch} to ADC_{ch} (SMA-SMA cable or jumper)")
            ok = True
            details = []
            for volts in (0.5, 2.5):
                self.board.io.set_dac(ch, volts)
                value = self.board.io.read_adc(ch)
                details.append(f"{volts:.1f}->{value:.3f} V")
                if abs(value - volts) > 0.05:
                    ok = False
            self.board.io.set_dac(ch, 0.0)
            self.auto(f"ADC_{ch} loopback", ok, ", ".join(details))

    def section_gpio(self) -> None:
        self.banner("GPIOs (13, 25-32)")
        # GPIO29/30 have no level shifter (analog-capable, 3.3 V straight off
        # the Teensy): a 3.3 V high may sit below the 5 V-logic D_IN threshold,
        # so they get a voltmeter check instead of the D_IN_1 loopback.
        shifted = [13, 25, 26, 27, 28, 31, 32]
        direct = [29, 30]

        self.board.disable()
        for gpio in shifted:
            self.board.io.setup_gpio(gpio, direction="output")
        direct_ok = []
        for gpio in direct:
            try:
                self.board.io.setup_gpio(gpio, direction="output")
                direct_ok.append(gpio)
            except SyncBoardError as exc:
                print(f"  Skipping GPIO{gpio}: {exc}")  # e.g. claimed by the magnet board
        self.board.enable()

        print("  Level-shifted GPIOs loop back into D_IN_1. Set each GPIO's")
        print("  level-select jumper to 5 V, or the input may not register.")
        # Per the schematic, GPIO29-32 surface on other connectors:
        #   J1-20: GPIO29 -> Ain0    J1-22: GPIO31 -> PWM0
        #   J1-21: GPIO30 -> Ain1    J1-23: GPIO32 -> PWM1
        # (the other jumper position routes them to the expansion headers).
        self.instruct("Set J1-20/J1-21 toward Ain0/Ain1 and J1-22/J1-23 toward "
                      "PWM0/PWM1 (not the expansion side)")
        probePoint = {31: "the PWM0 connector (= GPIO31 via J1-22)",
                      32: "the PWM1 connector (= GPIO32 via J1-23)"}
        for gpio in shifted:
            self.instruct(f"Jumper {probePoint.get(gpio, f'GPIO{gpio}')} to D_IN_1")
            self.board.io.write_gpio(gpio, True)
            high = self.board.io.read_di(1)
            self.board.io.write_gpio(gpio, False)
            low = self.board.io.read_di(1)
            self.auto(f"GPIO{gpio} drives D_IN_1", high and not low,
                      f"high={high}, low={low}")
        ainPoint = {29: "the Ain0 connector (= GPIO29 via J1-20)",
                    30: "the Ain1 connector (= GPIO30 via J1-21)"}
        for gpio in direct_ok:
            self.board.io.write_gpio(gpio, True)
            self.confirm(f"GPIO{gpio} high",
                         f"{ainPoint[gpio]} reads ~3.3 V? (no level shifter on this pin)")
            self.board.io.write_gpio(gpio, False)

        # Leave everything as it was: disabled, high-impedance inputs.
        self.board.disable()
        for gpio in shifted + direct_ok:
            self.board.io.setup_gpio(gpio, direction="input", enabled=False)
        self.board.enable()

    def section_switches(self) -> None:
        self.banner("12 V power switches (low-side drivers, channels 1-16)")
        print("  Channel n is net Po<n-1> on the power-switch headers. These are")
        print("  LOW-SIDE switches: a load connects between +12 V and the Po pin,")
        print("  and 'on' sinks the pin to GND. With no load attached, expect:")
        print("    on  -> ~0 V at the pin (hard low)")
        print("    off -> floating, drifting high (open-load diagnostic current)")
        # One channel per driver chip (S1..S4 handle 4 channels each), plus
        # channel 16 to cover the last PWM register.
        for ch in (1, 5, 9, 13, 16):
            net = f"Po{ch - 1}"
            self.board.io.set_switch(ch, 1.0)
            self.confirm(f"switch {ch} on", f"{net} reads ~0 V (pulled to GND)?")
            self.board.io.set_switch(ch, 0.0)
            self.confirm(f"switch {ch} off", f"{net} no longer pulled low?")

    def section_signals(self) -> None:
        self.banner("Signal engine")
        # 100 Hz square wave on DAC channel 1, repeating.
        print("  Loading a repeating 100 Hz 0/3 V square wave onto DAC channel 1...")
        self.board.signals.configure(0, SignalMode.DAC, option=1, repeat=True)
        self.board.signals.load(0, values=[3.0, 0.0], delays_ms=[5, 5])
        self.board.signals.start(0)
        self.confirm("signal DAC wave", "Scope DAC1: clean 100 Hz square wave, 0-3 V?")
        self.board.signals.stop(0)
        self.board.io.set_dac(1, 0.0)

        # Conductor + slave: DO1 stepped by a conductor at 20 Hz.
        print("  Conductor at 20 Hz driving DO1 as a slave...")
        self.board.signals.configure(1, SignalMode.CONDUCTOR, option=0b100, repeat=True)
        self.board.signals.load_uniform(1, 2, interval_ms=25)
        self.board.signals.configure(2, SignalMode.DO, option=1, repeat=True, is_slave=True)
        self.board.signals.load(2, values=[1.0, 0.0])
        self.board.signals.start(1)
        self.confirm("conductor/slave", "Scope D_OUT_1: 20 Hz square wave?")
        self.board.signals.stop(1)
        self.board.io.write_do(1, False)

        # ADC recording: 200 samples at 2 ms.
        print("  Recording ADC channel 1 for 0.4 s...")
        self.board.signals.configure(3, SignalMode.ADC, option=1)
        self.board.signals.load_uniform(3, 200, interval_ms=2)
        self.board.signals.start(3)
        self.board.signals.wait(3, timeout=2.0)
        data = self.board.signals.read(3)
        print(f"  Recorded {len(data)} samples, first 5: "
              f"{[f'{v:.3f}' for v in data[:5]]}")
        self.auto("signal ADC record", len(data) == 200)

    def section_led(self, channel: int) -> None:
        self.banner(f"LED board (channel {channel})")
        answer = input("  Run LED calibration first? Needed once per LED. [y/N]: ")
        if answer.strip().lower() == "y":
            raw = input("  Max LED current in A (blank = channel's rated default): ").strip()
            print("  Calibrating (takes a few seconds, LED will flash)...")
            self.board.leds.calibrate(channel, float(raw) if raw else None)
            self.auto("LED calibration", True, "completed without error")
        self.board.leds.set_level(channel, 0.05)
        print("  Pulsing the LED at 5% for 500 ms...")
        self.board.leds.pulse(channel, 500)
        self.confirm("LED pulse", "Did the LED light for half a second?")
        measurement_error = None
        self.board.leds.on(channel)
        try:
            m = self.board.leds.measure(channel)
            print(f"  While on: current {m.current_a:.3f} A, optical {m.optical_mv:.1f} mV")
        except SyncBoardError as exc:
            measurement_error = exc
            print(f"  Measurement failed: {exc}")
        finally:
            self.board.leds.off(channel)
        self.auto("LED measure", measurement_error is None)

    def section_magnet(self) -> None:
        self.banner("Magnet board")
        # Rerun setup in case an earlier section power-cycled the system.
        self.board.magnet.setup()
        self.auto("magnet chips detected", True)
        for hall_id in range(3):
            value = self.board.magnet.read_hall(hall_id)
            print(f"  Hall {hall_id}: {value:+.3f} mT")
        self.confirm("hall zero", "With no magnet nearby, are those near 0 mT?")
        answer = input("  Run magnet calibration? Drives current through the coil. [y/N]: ")
        if answer.strip().lower() == "y":
            print("  Calibrating (takes ~10 s)...")
            self.board.magnet.calibrate()
            self.auto("magnet calibration", True, "completed without error")
            self.board.magnet.enable(True)
            self.board.magnet.set_current(0.1)
            time.sleep(0.5)
            print(f"  At setpoint 0.1: current monitor = "
                  f"{self.board.magnet.read_adc(1):.4f} V")
            self.confirm("magnet current", "Does the coil current look plausible "
                         "(monitor away from its zero value / meter shows current)?")
            self.board.magnet.set_current(0.0)
            self.board.magnet.enable(False)

    def section_imaging(self) -> None:
        self.banner("Imaging (camera trigger)")
        self.board.imaging.set_sync_mode(1)
        self.board.imaging.configure([Frame(exposure_ms=100)])
        print("  Starting a 1-frame sequence (camera trigger pin 32, TrI)...")
        self.instruct("Scope the camera trigger line; trigger on a rising edge")
        self.board.imaging.start()
        self.board.imaging.wait(timeout=2.0)
        self.confirm("camera trigger", "Did you catch a ~1 ms high pulse?")
        self.board.imaging.set_sync_mode(0)


CORE_SECTIONS = ["system", "do", "di", "dac", "adc", "gpio", "switches", "signals"]
ALL_SECTIONS = CORE_SECTIONS + ["led", "magnet", "imaging"]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", help="serial port (default: auto-discover)")
    parser.add_argument("--sections", help=f"comma-separated subset of: {','.join(ALL_SECTIONS)}")
    parser.add_argument("--led", type=int, metavar="CH",
                        help="include the LED section, testing this channel (1-8)")
    parser.add_argument("--magnet", action="store_true", help="include the magnet section")
    parser.add_argument("--list", action="store_true", help="list sections and exit")
    args = parser.parse_args()

    if args.list:
        print("\n".join(ALL_SECTIONS))
        return 0

    logging.basicConfig(level=logging.INFO, format="  [%(levelname)s] %(message)s")

    sections = args.sections.split(",") if args.sections else list(CORE_SECTIONS)
    if args.led is not None and "led" not in sections:
        sections.append("led")
    if args.magnet and "magnet" not in sections:
        sections.append("magnet")
    unknown = set(sections) - set(ALL_SECTIONS)
    if unknown:
        parser.error(f"unknown sections: {', '.join(sorted(unknown))}")
    if "led" in sections and args.led is None:
        parser.error("the led section needs --led CH to pick a safe channel")

    print("Connecting...")
    try:
        board = SyncBoard.connect(args.port)
    except SyncBoardError as exc:
        print(f"Could not connect: {exc}")
        print("Is the board plugged in and flashed with firmware v2?")
        return 2
    checkout = Checkout(board)
    try:
        board.initialise(led_board="led" in sections,
                         magnet_board="magnet" in sections)
        for name in sections:
            if name == "led":
                checkout.section_led(args.led)
            elif name == "system":
                checkout.section_system()
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
