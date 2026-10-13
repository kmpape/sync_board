#!/usr/bin/env python3
"""Interactive bring-up for the screener board (ScreenerMobo) in the
SyncBoard's Expansion 2 header.

Pick a test from the menu, check or change its options, and run it. You come
back to the menu afterwards, so tests run in any order and can be repeated
with different options. Every test ends with all screener outputs off.
Ctrl+C stops the running test and returns to the menu; q quits and prints a
pass/fail summary.

Tests are independent: a chip that does not answer at setup only skips the
tests that cannot work without it (others degrade, e.g. rf-adc still reads
the AD8302 channels without the PCA9685 to enable the AD8318s), and inside
a test each channel or reading is a separate check, so one failure is
recorded and the test carries on.

Examples:
    python hwtest/screener_board.py
    python hwtest/screener_board.py --port /dev/cu.usbmodem1234

At the options prompt, press Enter to run, type name=value (several at once
are fine) to change options, e to step through them all, or b to go back.

Before running:
  - Screener board in Expansion 2 (SyncBoard H4), powered from its own 12 V
    supply on H2 (LL-5 lit).
  - For the field and air tests: the FieldSensor flex plugged into the
    AirSensor, and the AirSensor's cable into Con_S.
  - SyncBoard flashed with firmware 2.2.0+ (2.1.0 has no sensor boards).
See docs/bringup/screener-board.md and sensor-boards.md for what each test
covers.
"""

from __future__ import annotations

import argparse
import contextlib
import logging
import pathlib
import statistics
import sys
import textwrap
import threading
import time
from dataclasses import dataclass, field
from typing import Any, Callable

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))

from checkout import Checkout  # noqa: E402
from syncboard import (  # noqa: E402
    AirReference,
    CommandError,
    ScreenerChips,
    ScreenerEnable,
    ScreenerRef,
    SyncBoard,
    SyncBoardError,
    pt1000_celsius,
    sht40_celsius,
    sht40_rh_percent,
)
from syncboard.board import AirSensorApi, FieldSensorApi, ScreenerApi  # noqa: E402

# -- option parsing ----------------------------------------------------------


def parse_bool(text: str) -> bool:
    value = text.strip().lower()
    if value in ("y", "yes", "1", "true", "on"):
        return True
    if value in ("n", "no", "0", "false", "off"):
        return False
    raise ValueError("expected yes or no")


def int_in(lo: int, hi: int) -> Callable[[str], int]:
    def parse(text: str) -> int:
        value = int(text)
        if not lo <= value <= hi:
            raise ValueError(f"must be {lo}..{hi}")
        return value
    return parse


def float_in(lo: float, hi: float) -> Callable[[str], float]:
    def parse(text: str) -> float:
        value = float(text)
        if not lo <= value <= hi:
            raise ValueError(f"must be {lo:g}..{hi:g}")
        return value
    return parse


def optional_float(text: str) -> float | None:
    return None if text.strip() in ("", "-") else float(text)


def one_of(*names: str) -> Callable[[str], str]:
    def parse(text: str) -> str:
        value = text.strip().lower()
        if value not in names:
            raise ValueError(f"must be one of: {', '.join(names)}")
        return value
    return parse


def names_from(*names: str) -> Callable[[str], list[str]]:
    """Comma-separated subset of names, or 'all' (keeps the given order)."""
    def parse(text: str) -> list[str]:
        value = text.strip().lower()
        if value == "all":
            return list(names)
        chosen = [n.strip() for n in value.split(",") if n.strip()]
        unknown = [n for n in chosen if n not in names]
        if unknown or not chosen:
            raise ValueError(f"use 'all' or a comma-separated list of: {', '.join(names)}")
        return [n for n in names if n in chosen]
    return parse


def channel_list(lo: int, hi: int) -> Callable[[str], list[int]]:
    """'all', or channels and ranges like '1-3,5'."""
    def parse(text: str) -> list[int]:
        value = text.strip().lower()
        if value == "all":
            return list(range(lo, hi + 1))
        channels: list[int] = []
        for part in value.split(","):
            first, _, last = part.strip().partition("-")
            start, end = int(first), int(last or first)
            if not (lo <= start <= end <= hi):
                raise ValueError(f"channels must be within {lo}..{hi}")
            channels += [c for c in range(start, end + 1) if c not in channels]
        return channels
    return parse


@dataclass
class Option:
    name: str
    default: str  # as the operator would type it
    help: str
    parse: Callable[[str], Any] = str


@dataclass
class Test:
    key: str
    title: str
    about: str
    run: Callable[[dict[str, Any]], None]
    options: list[Option] = field(default_factory=list)
    needs_setup: bool = True
    needs: tuple[str, ...] = ()  # ScreenerChips fields this test cannot run without
    last: str = ""
    texts: dict[str, str] = field(init=False)

    def __post_init__(self) -> None:
        self.texts = {o.name: o.default for o in self.options}
        self.values()  # fail fast on a bad default

    def values(self) -> dict[str, Any]:
        return {o.name: o.parse(self.texts[o.name]) for o in self.options}


def repeat(count: int, interval_s: float, body: Callable[[int], None]) -> None:
    """Calls body(round) count times, or until Ctrl+C when count is 0."""
    i = 0
    try:
        while count == 0 or i < count:
            if i and interval_s > 0:
                time.sleep(interval_s)
            body(i)
            i += 1
    except KeyboardInterrupt:
        if count:
            raise
        print("\n  (stopped)")


def describe(values: list[float], unit: str, scale: float = 1.0, digits: int = 4) -> str:
    """mean, std, min..max of values (times scale), for repeated readings."""
    scaled = [v * scale for v in values]
    text = f"mean {statistics.fmean(scaled):.{digits}f} {unit}"
    if len(scaled) > 1:
        text += (f", std {statistics.stdev(scaled):.{digits}f}, "
                 f"range {min(scaled):.{digits}f}..{max(scaled):.{digits}f}")
    return text


# -- the bring-up session ----------------------------------------------------

AINPUTS = {
    0: "RTD sense + (via mux)", 1: "RTD sense - (via mux)", 2: "FluxgateP", 3: "FluxgateN",
    4: "HallSignal (B)", 5: "2V5_Offset", 6: "HallRefP", 7: "HallRefN", 8: "T_RH",
    9: "RH", 10: "RTDI (IDAC pin)", 11: "unconnected", 12: "AINCOM (GND)",
}

CHIP_NAMES = {"pca9685": "PCA9685 (0x51)", "ads7828": "ADS7828 (0x4B)",
              "ads124s08": "ADS124S08 (SPI)"}

ENABLES = {
    "rf": ScreenerEnable.RF, "rf-spare": ScreenerEnable.RF_SPARE,
    "fluxgate": ScreenerEnable.FLUXGATE, "sensors": ScreenerEnable.SENSORS,
}


class ScreenerBringup(Checkout):

    def __init__(self, board: SyncBoard):
        super().__init__(board)
        self.sc = board.screener
        self.field = board.screener.field_sensor
        self.air = board.screener.air_sensor
        self.ready = False  # setupScreener has run since the last enable
        self.chips: ScreenerChips | None = None  # what that setup found
        self.tests = self.build_tests()

    # -- session plumbing ------------------------------------------------------

    def have(self, chip: str) -> bool:
        return self.chips is not None and getattr(self.chips, chip)

    @contextlib.contextmanager
    def step(self, name: str):
        """One independent step: a board error fails just this step, and the
        test carries on with the next."""
        try:
            yield
        except SyncBoardError as exc:
            print(f"  FAIL: {name} ({exc})")
            self.results.append((name, "FAIL"))

    def ensure_setup(self, quiet: bool = False) -> bool:
        """Enables and sets up the board if needed. Only fails when the
        SyncBoard itself refuses; missing screener chips are just reported."""
        if self.ready:
            return True
        try:
            if not self.board.status().enabled:
                self.board.enable()
            self.chips = self.sc.setup()
        except SyncBoardError as exc:
            print(f"  Screener setup failed: {exc}")
            return False
        self.ready = True
        if not quiet:
            print("  Screener chips: " + ", ".join(
                f"{label} {'found' if self.have(chip) else 'MISSING'}"
                for chip, label in CHIP_NAMES.items()))
        return True

    def outputs_off(self) -> None:
        """Switches off whatever this setup can drive, each part independently."""
        if not self.ready:
            return  # nothing can be on
        steps = [lambda: self.sc.set_rf_switch(False)]
        if self.have("pca9685"):
            steps.insert(0, lambda: self.sc.set_pwm(0, 0.0))
        for off in steps:
            try:
                off()
            except SyncBoardError as exc:
                print(f"  Could not switch an output off: {exc}")

    def run_test(self, test: Test) -> None:
        values = test.values()
        start = len(self.results)
        outcome = None
        self.banner(f"{test.key}: {test.title}")
        try:
            if test.needs_setup and not self.ensure_setup():
                test.last = "not ready"
                return
            missing = [c for c in test.needs if not self.have(c)]
            if missing:
                print(f"  Skipped: needs the {' and '.join(CHIP_NAMES[c] for c in missing)}, "
                      "which did not answer at setup ('chips' probes again).")
                test.last = "skipped: no " + ", ".join(c.upper() for c in missing)
                return
            test.run(values)
        except KeyboardInterrupt:
            print("\n  Stopped.")
            outcome = "stopped"
        except SyncBoardError as exc:
            print(f"  Error: {exc}")
            self.results.append((f"{test.key}: {exc}", "FAIL"))
        finally:
            self.outputs_off()
        counts = {r: sum(1 for _, x in self.results[start:] if x == r)
                  for r in ("FAIL", "PASS", "SKIP")}
        summary = ", ".join(f"{n} {r.lower()}" for r, n in counts.items() if n)
        test.last = outcome or summary or "done"

    def find(self, choice: str) -> Test | None:
        if choice.isdigit() and 1 <= int(choice) <= len(self.tests):
            return self.tests[int(choice) - 1]
        matches = [t for t in self.tests if t.key == choice] or \
                  [t for t in self.tests if t.key.startswith(choice)]
        return matches[0] if len(matches) == 1 else None

    def print_menu(self) -> None:
        print("\n" + "=" * 64)
        print("  Screener board bring-up")
        for i, t in enumerate(self.tests, 1):
            last = f"[{t.last}]" if t.last else ""
            print(f"  {i:>2}  {t.key:<10} {t.title:<42} {last}")
        print("   r  repeat last test     o  outputs off     s  status     q  quit")

    def review_options(self, test: Test) -> bool:
        """Shows a test's options until the operator runs it (True) or backs
        out (False)."""
        self.banner(f"{test.key}: {test.title}")
        print(textwrap.fill(test.about, width=76, initial_indent="  ", subsequent_indent="  "))
        while True:
            if test.options:
                print("\n  Options:")
                width = max(len(o.name) for o in test.options)
                for o in test.options:
                    print(f"    {o.name:<{width}} = {test.texts[o.name]:<8} {o.help}")
                prompt = "\n  Enter = run, name=value to change, e = edit each, b = back > "
            else:
                prompt = "\n  Enter = run, b = back > "
            answer = input(prompt).strip()
            if not answer:
                return True
            if answer.lower() == "b":
                return False
            if answer.lower() == "e" and test.options:
                for o in test.options:
                    self.edit_option(test, o)
                continue
            self.assign_options(test, answer)

    @staticmethod
    def edit_option(test: Test, option: Option) -> None:
        while True:
            raw = input(f"    {option.name} [{test.texts[option.name]}] ({option.help}): ").strip()
            if not raw:
                return
            try:
                option.parse(raw)
            except ValueError as exc:
                print(f"    {exc}")
                continue
            test.texts[option.name] = raw
            return

    @staticmethod
    def assign_options(test: Test, text: str) -> None:
        for token in text.split():
            name, sep, raw = token.partition("=")
            matches = [o for o in test.options if o.name == name] or \
                      [o for o in test.options if o.name.startswith(name)]
            if not sep or len(matches) != 1:
                print(f"  '{token}': expected name=value with one of: "
                      f"{', '.join(o.name for o in test.options)}")
                continue
            try:
                matches[0].parse(raw)
            except ValueError as exc:
                print(f"  {matches[0].name}: {exc}")
                continue
            test.texts[matches[0].name] = raw

    def show_status(self) -> None:
        status = self.board.status()
        print(f"  firmware {self.board.ping()}, system {'enabled' if status.enabled else 'disabled'}, "
              f"screener {'attached' if status.screener_board_attached else 'NOT attached'}, "
              f"{'set up' if self.ready else 'not set up'}")

    def menu(self) -> None:
        last: Test | None = None
        while True:
            self.print_menu()
            try:
                choice = input("\n  choose> ").strip().lower()
            except (EOFError, KeyboardInterrupt):
                print()
                return
            try:
                if choice == "q":
                    return
                if choice == "o":
                    self.outputs_off()
                    print("  All screener outputs off.")
                elif choice == "s":
                    self.show_status()
                elif choice == "r":
                    if last is None:
                        print("  Nothing has run yet.")
                    else:
                        self.run_test(last)
                elif choice:
                    test = self.find(choice)
                    if test is None:
                        print(f"  No test '{choice}'.")
                    elif self.review_options(test):
                        self.run_test(test)
                        last = test
            except KeyboardInterrupt:
                print()  # Ctrl+C at an options prompt: back to the menu
            except EOFError:
                print()
                return  # input closed: quit
            except SyncBoardError as exc:
                print(f"  Error: {exc}")

    # -- tests -----------------------------------------------------------------

    def build_tests(self) -> list[Test]:
        return [
            Test("link", "Isolated link: heartbeat and I2C", (
                "The chain from the SyncBoard across the isolators: the board's 12 V "
                "indicator, the isolated heartbeat (ENWD, LL-6) coming up with enable, "
                "both I2C chips answering through Iso-1, and (optionally) ENWD dropping "
                "again when the system is disabled."),
                self.test_link, needs_setup=False, options=[
                    Option("interlock", "yes", "also disable the system and check everything drops",
                           parse_bool),
                ]),
            Test("chips", "Chip probe and ADC supply monitors", (
                "Runs setupScreener again, which probes each chip on its own: the PCA9685 "
                "(0x51) and ADS7828 (0x4B) over I2C through Iso-1, the ADS124S08 over SPI "
                "through Iso-2 (pattern write/read-back). Then, for whichever answered, reads "
                "the ADS7828 and the ADS124S08's supply monitors: AVDD is the board's "
                "heartbeat-gated 5 V analog rail (VReg-3), DVDD its always-on 3.3 V "
                "(VReg-5). Chips left out of 'fitted' are reported but not checked."),
                self.test_chips, needs_setup=False, options=[
                    Option("fitted", "all", "chips that should answer: pca9685, ads7828, "
                           "ads124s08 (comma list) or all", names_from(*CHIP_NAMES)),
                    Option("tol", "5", "% allowed on the supply readings", float_in(0.1, 50)),
                ]),
            Test("enables", "Rail and detector enables", (
                "Turns each enable on and off in turn and asks about its indicator: LL-2 "
                "RF detectors 1-3, LL-3 fluxgate rail, LL-4 sensors rail (flex sensors, "
                "LED displays, RTD muxes). The spare RF detector's enable has no "
                "indicator; probe its ENBL pin."),
                self.test_enables, needs=("pca9685",), options=[
                    Option("which", "all", "rf, rf-spare, fluxgate, sensors (comma list) or all",
                           names_from(*ENABLES)),
                    Option("rails", "yes", "also check the switched rails at Con_S with a meter",
                           parse_bool),
                ]),
            Test("switches", "Power switches 1-7", (
                "Each channel is a low-side HITFET: its load sits between HP-1 pin 1 (12 V) "
                "and pin 2 (VL), and 'on' pulls VL to ground. The channel's LL-1 follows "
                "the switch input, so it lights even with no load. Without a load VL just "
                "floats when off; a small load (12 V lamp or resistor) makes the check "
                "clear. mode=pwm holds a duty cycle for the scope (~200 Hz)."),
                self.test_switches, needs=("pca9685",), options=[
                    Option("channels", "1-7", "switch channels, e.g. 1-7 or 2,5",
                           channel_list(1, ScreenerApi.NUM_SWITCHES)),
                    Option("mode", "steady", "steady (on then off) or pwm", one_of("steady", "pwm")),
                    Option("duty", "0.5", "duty for mode=pwm", float_in(0.0, 1.0)),
                ]),
            Test("rf-adc", "RF detector outputs (ADS7828)", (
                "Reads the eight ADS7828 channels, 0-1.8 V (the reference is AD8302 #1's "
                "VREF). AD8318 outputs fall as RF power rises (about -25 mV/dB); AD8302 "
                "gain reads 0.9 V for equal inputs (30 mV/dB) and phase 0.9 V at 90 deg "
                "(-10 mV/deg). To see them respond, feed a generator into Con1-Con4 near "
                "480 MHz (the input bandpass filters), 4 dBm maximum."),
                self.test_rf_adc, needs=("ads7828",), options=[
                    Option("enable", "yes", "turn both RF detector enables on for the test",
                           parse_bool),
                    Option("rounds", "3", "rounds of readings (0 = until Ctrl+C)", int_in(0, 10000)),
                    Option("interval", "1.0", "seconds between rounds", float_in(0.0, 60.0)),
                ]),
            Test("rf-switch", "RF switch control line", (
                "GPIO31 (Teensy pin 14) -> SyncBoard J1-22 -> Expansion 2 pin 3 -> Iso-2 -> "
                "Buf1 -> 5 V TTL on the centre pin of RFS1, which drives the ZYSWA-2-50DR. "
                "J1-22 must be on pins 1-2 (expansion); the SyncBoard checkout's gpio "
                "section moves it to PWM0. mode=toggle runs a square wave for the scope."),
                self.test_rf_switch, options=[
                    Option("mode", "steady", "steady (high then low) or toggle",
                           one_of("steady", "toggle")),
                    Option("period_ms", "500", "toggle period, ms", int_in(20, 10000)),
                    Option("supply", "yes", "also check the switch's +-5 V supply on HP-4",
                           parse_bool),
                ]),
            Test("rtd", "RTD channels 1-8 (4-wire)", (
                "The ADS124S08's 500 uA IDAC drives the RTD and returns through the "
                "4.7 kOhm 0.01 % reference resistor; the RTD's sense pair is read at gain 2 "
                "against that resistor's voltage, so full scale is 2350 ohm. HRTD headers "
                "(channels 1-6): pins 1 (D+) and 3 (V+) to one end of the RTD, pins 2 (D-) "
                "and 4 (V-) to the other. Channels 7 and 8 come in on the sensor flex. "
                "The default reading is chopped in firmware: the mean of six conversions "
                "with the excitation reversed between each (~1.1 s). polarity=both takes "
                "one conversion at each polarity instead and shows half their difference, "
                "the wiring offset that chopping cancels. Readings are sign-corrected for "
                "the excitation polarity, so both polarities should agree; a negative "
                "value means the V pair is swapped against the D pair. A 1 kOhm 0.1 % "
                "resistor wired 4-wire makes a good dummy (0 C on a PT1000). The muxes run "
                "from the sensors rail, which each reading powers and switches off again "
                "(LL-4 flashes)."),
                self.test_rtd, needs=("ads124s08", "pca9685"), options=[
                    Option("channels", "1-8", "RTD channels, e.g. 1-6 or 3,7",
                           channel_list(1, ScreenerApi.NUM_RTDS)),
                    Option("polarity", "chop", "chop (6-reading mean), 0, 1 or both",
                           one_of("chop", "0", "1", "both")),
                    Option("rounds", "1", "passes over the channels (0 = until Ctrl+C)",
                           int_in(0, 10000)),
                    Option("expect", "-", "ohms of a known resistor on every channel (- = none)",
                           optional_float),
                    Option("tol", "0.1", "% allowed from 'expect'", float_in(0.001, 100)),
                ]),
            Test("field", "FieldSensor: supply, Hall, fluxgate, RTD 7", (
                "The FieldSensor flex, plugged into the AirSensor and through its cable into "
                "Con_S. Each reading powers its rail only while it runs, as the driver "
                "does for every reading, so LL-4 (sensors) and LL-3 (fluxgate) flash with "
                "each one. Reads the flex 5 V as the Hall sensor sees it, the Hall sensor "
                "(0.5 of its supply at zero field, 200 mT per unit), the fluxgate (VOUT - "
                "REFOUT, 0.732 V/mT, +-2 mT) and the PT1000 on RTD channel 7. sweep=yes "
                "steps the settle time after power-on and checks the firmware's default "
                "is long enough; heat_s holds the fluxgate on and tracks RTD 7 to show its "
                "self-heating. Without the SPI ADC it holds the rails on and asks for meter "
                "readings at the C8 caps instead."),
                self.test_field, needs=("pca9685",), options=[
                    Option("which", "all", "supply, hall, fluxgate, rtd (comma list) or all",
                           names_from("supply", "hall", "fluxgate", "rtd")),
                    Option("rounds", "3", "rounds of readings (0 = until Ctrl+C)", int_in(0, 10000)),
                    Option("count", "1", "conversions per reading, in one power-on",
                           int_in(1, FieldSensorApi.MAX_COUNT)),
                    Option("settle_ms", "-", "settle after power-on, ms (- = firmware default)",
                           optional_float),
                    Option("sweep", "no", "step the settle time (Hall, fluxgate)", parse_bool),
                    Option("rest_s", "2", "rail off before each sweep reading, s",
                           float_in(0.0, 60.0)),
                    Option("heat_s", "0", "hold the fluxgate on this long, watching RTD 7 "
                           "(0 = skip)", int_in(0, 600)),
                ]),
            Test("air", "AirSensor: humidity sensor, RTD 8", (
                "The AirSensor's SHT40I (analog RH and T, 10-90 % of its 5 V supply) and the "
                "PT1000 on RTD channel 8, which sit together in the airflow, so the two "
                "temperatures should agree. Each reading powers the sensors rail and waits "
                "for the SHT40I's outputs to settle (200 ms), so LL-4 flashes. "
                "reference=flex reads the outputs against the flex 5 V as the FieldSensor "
                "senses it (needs the FieldSensor); offset assumes the nominal 5 V; auto "
                "picks flex when the FieldSensor answers; both shows the two side by side. "
                "Without the SPI ADC it asks for meter readings at the C8 caps instead."),
                self.test_air, needs=("pca9685",), options=[
                    Option("reference", "auto", "auto, flex, offset or both",
                           one_of("auto", "flex", "offset", "both")),
                    Option("rounds", "3", "rounds of readings (0 = until Ctrl+C)", int_in(0, 10000)),
                    Option("settle_ms", "-", "settle after power-on, ms (- = firmware default)",
                           optional_float),
                    Option("rtd", "yes", "also read RTD 8 and compare", parse_bool),
                    Option("tol", "1.0", "C allowed between the SHT40I and RTD 8",
                           float_in(0.05, 20)),
                    Option("sweep", "no", "step the settle time", parse_bool),
                    Option("rest_s", "2", "rail off before each sweep reading, s",
                           float_in(0.0, 60.0)),
                ]),
            Test("adc", "Any ADS124S08 input pair", (
                "Repeated conversions of one input pair with statistics. Inputs: "
                + "; ".join(f"{k} {v}" for k, v in AINPUTS.items())
                + ". ref is internal (2.5 V), rtd (REFP0/REFN0) or hall (AIN6/AIN7). "
                "Gain 1 bypasses the PGA; above that the inputs need ~0.15 V of headroom "
                "from the rails."),
                self.test_adc, needs=("ads124s08",), options=[
                    Option("ainp", "5", "positive input, 0-11 or 12 = AINCOM", int_in(0, 12)),
                    Option("ainn", "12", "negative input, 0-11 or 12 = AINCOM", int_in(0, 12)),
                    Option("ref", "internal", "internal, rtd or hall", one_of("internal", "rtd", "hall")),
                    Option("gain", "1", "1, 2, 4, ... 128",
                           lambda t: int(one_of(*(str(1 << b) for b in range(8)))(t))),
                    Option("count", "10", "readings (0 = until Ctrl+C)", int_in(0, 100000)),
                    Option("sensors", "no", "turn the sensors (and fluxgate) enables on first",
                           parse_bool),
                ]),
            Test("displays", "LED bar displays", (
                "The five bar arrays run from the sensors rail and show the RF and field "
                "signals in hardware. LR-1..LR-3 (LM3914): RFA (forward power), RFB (gain) "
                "and RFC (phase), taken from AD8302 #1 or #2 per J1-1/J1-2 (default RF12). "
                "LR-4/LR-5 (LM3915): the Hall field B as two bars from a common zero, "
                "positive field lighting L10 (green) first. J1-3 sets the bar reference."),
                self.test_displays, needs=("pca9685",), options=[
                    Option("rf", "yes", "also enable the RF detectors, so the RF bars are live",
                           parse_bool),
                ]),
            Test("pwm", "Raw PCA9685 channel", (
                "Drives one PCA9685 output directly, for probing. Channels: "
                + "; ".join(f"{c} {n}" for c, n in ScreenerApi.PWM_CHANNELS.items())
                + ". It is switched back off when you press Enter."),
                self.test_pwm, needs=("pca9685",), options=[
                    Option("channel", "3", "PCA9685 channel 1-16", int_in(1, 16)),
                    Option("duty", "1.0", "0..1 (fractional = ~200 Hz PWM)", float_in(0.0, 1.0)),
                ]),
        ]

    def test_link(self, opts: dict[str, Any]) -> None:
        self.confirm("12 V input", "LL-5 (12 V) lit?")
        with self.step("enable"):
            if not self.board.status().enabled:
                self.board.enable()
        self.confirm("heartbeat up", "LL-6 (ENWD, the isolated heartbeat) lit? "
                     "(N1 pin 2 ~3.3 V, PWM1 pin 23 (OE) ~0 V)")
        with self.step("I2C scan"):
            addresses = self.board.scan_i2c()
            print(f"  I2C devices: {[hex(a) for a in addresses]}")
            self.auto("PCA9685 at 0x51", 0x51 in addresses)
            self.auto("ADS7828 at 0x4B", 0x4B in addresses)
        if opts["interlock"]:
            with self.step("interlock"):
                print("  Disabling: the heartbeat stops, so ENWD should drop...")
                self.ready = False
                self.board.disable()
                self.confirm("interlock", "LL-6 off, and PWM1 pin 23 (OE) back at ~3.3 V?")
                self.board.enable()
        self.ready = False
        self.ensure_setup()

    def test_chips(self, opts: dict[str, Any]) -> None:
        self.ready = False
        if not self.ensure_setup():
            self.auto("setupScreener", False)
            return
        for chip, label in CHIP_NAMES.items():
            if chip in opts["fitted"]:
                self.auto(f"{label} answers", self.have(chip))
            else:
                print(f"  {label}: {'answers' if self.have(chip) else 'absent'} (not checked)")
        if self.have("ads7828"):
            with self.step("ADS7828 reads"):
                readings = [self.sc.read_rf(ch) for ch in ScreenerApi.RF_CHANNELS]
                print("  RF ADC: " + ", ".join(f"{v:.3f}" for v in readings) + " V")
        if self.have("ads124s08"):
            tol = opts["tol"] / 100
            for analog, name, nominal in ((True, "AVDD (5 V analog rail)", 5.0),
                                          (False, "DVDD (3.3 V digital)", 3.3)):
                with self.step(f"ADS124S08 {name}"):
                    volts = self.sc.read_supply(analog)
                    self.auto(f"ADS124S08 {name}", abs(volts - nominal) <= tol * nominal,
                              f"{volts:.3f} V")

    def test_enables(self, opts: dict[str, Any]) -> None:
        indicator = {"rf": "LL-2", "fluxgate": "LL-3", "sensors": "LL-4"}
        rf_watch = {"rf": (1, 4, 7), "rf-spare": (8,)}
        rail = {"fluxgate": "Con_S pin 3 (3V3_Fluxgate_Con, or CF-5's connector side) ~3.3 V",
                "sensors": "Con_S pin 6 (5V_Sensors_Con, or CF-6's connector side) ~5 V"}
        for name in opts["which"]:
            with self.step(f"{name} enable"):
                # The detector outputs are a bonus check; they need the ADS7828.
                watch = rf_watch.get(name, ()) if self.have("ads7828") else ()
                before: list[float] = []
                with self.step(f"{name} detector readings"):
                    before = [self.sc.read_rf(ch) for ch in watch]
                self.sc.set_enable(ENABLES[name], True)
                if watch and before:
                    with self.step(f"{name} detector readings"):
                        time.sleep(0.1)
                        after = [self.sc.read_rf(ch) for ch in watch]
                        print("  " + ", ".join(
                            f"{ScreenerApi.RF_CHANNELS[ch]} {b:.3f} -> {a:.3f} V"
                            for ch, b, a in zip(watch, before, after)))
                if name in indicator:
                    self.confirm(f"{name} enable on", f"{indicator[name]} lit?")
                else:
                    self.confirm(f"{name} enable on", "RF_Mag_4 pin 16 (ENBL) ~3.3 V? "
                                 "(this enable has no indicator)")
                if opts["rails"] and name in rail:
                    self.confirm(f"{name} rail", f"{rail[name]}?")
                self.sc.set_enable(ENABLES[name], False)
                if name in indicator:
                    self.confirm(f"{name} enable off", f"{indicator[name]} off again?")

    def test_switches(self, opts: dict[str, Any]) -> None:
        self.instruct("Loads (if any) on HP-1 of the channels under test, supply limit set "
                      "for them")
        for ch in opts["channels"]:
            with self.step(f"switch {ch}"):
                if opts["mode"] == "steady":
                    self.sc.set_switch(ch, 1.0)
                    self.confirm(f"switch {ch} on", f"Channel {ch}: LL-1 lit and HP-1 pin 2 "
                                 "(VL) pulled to ~0 V?")
                    self.sc.set_switch(ch, 0.0)
                    self.confirm(f"switch {ch} off", "LL-1 off and VL no longer pulled low?")
                else:
                    duty = opts["duty"]
                    self.sc.set_switch(ch, duty)
                    self.confirm(f"switch {ch} pwm {duty:.0%}",
                                 f"Scope channel {ch}'s HP-1 pin 2: ~200 Hz, pulled low for "
                                 f"{duty:.0%} of each period?")
                    self.sc.set_switch(ch, 0.0)

    def test_rf_adc(self, opts: dict[str, Any]) -> None:
        if opts["enable"]:
            if self.have("pca9685"):
                with self.step("RF detector enables"):
                    self.sc.set_enable(ScreenerEnable.RF, True)
                    self.sc.set_enable(ScreenerEnable.RF_SPARE, True)
                    time.sleep(0.1)
            else:
                print("  No PCA9685, so the AD8318 detectors (RF1-4_Mag) stay disabled; the "
                      "AD8302 gain/phase channels still read.")
        names = ScreenerApi.RF_CHANNELS
        print("  " + " ".join(f"{n:>8}" for n in names.values()))
        last: dict[int, float] = {}
        errors: dict[int, str] = {}

        def one_round(_: int) -> None:
            cells = []
            for ch in names:
                try:
                    last[ch] = self.sc.read_rf(ch)
                    cells.append(f"{last[ch]:>8.3f}")
                except SyncBoardError as exc:
                    errors.setdefault(ch, str(exc))
                    cells.append(f"{'err':>8}")
            print("  " + " ".join(cells))

        repeat(opts["rounds"], opts["interval"], one_round)
        for ch, message in errors.items():
            self.auto(f"RF ADC channel {ch} ({names[ch]}) reads", False, message)
        if all(ch in last for ch in (2, 3, 5, 6)):
            # Nominal AD8302 transfer functions from the datasheet.
            print(f"  Nominal: RF12 {(last[2] - 0.9) / 0.03:+.1f} dB, {180 - last[3] / 0.01:.0f} deg;"
                  f"  RF23 {(last[5] - 0.9) / 0.03:+.1f} dB, {180 - last[6] / 0.01:.0f} deg")
        if last:
            self.confirm("RF detectors", "Readings plausible, and (if you applied RF) did the "
                         "matching channels respond?")

    def test_rf_switch(self, opts: dict[str, Any]) -> None:
        self.instruct("SyncBoard J1-22 on pins 1-2 (expansion), meter or scope on the RFS1 "
                      "centre pin")
        if opts["supply"]:
            self.confirm("RF switch supply", "HP-4 pin 2 ~+5 V and pin 1 ~-5 V?")
        if opts["mode"] == "steady":
            for level, volts in ((True, 5), (False, 0)):
                name = f"RF switch control {'high' if level else 'low'}"
                with self.step(name):
                    self.sc.set_rf_switch(level)
                    self.confirm(name, f"RFS1 reads ~{volts} V?")
            return
        half_period_s = opts["period_ms"] / 2000
        print(f"  Toggling every {half_period_s * 1000:.0f} ms until you answer...")
        stop = threading.Event()

        def pump() -> None:
            level = False
            while not stop.is_set():
                level = not level
                try:
                    self.sc.set_rf_switch(level)
                except SyncBoardError as exc:
                    print(f"  toggling stopped: {exc}")
                    return
                stop.wait(half_period_s)

        pump_thread = threading.Thread(target=pump, daemon=True)
        pump_thread.start()
        try:
            self.confirm("RF switch toggles", f"RFS1: 0/5 V square wave at "
                         f"~{1000 / opts['period_ms']:.1f} Hz?")
        finally:
            stop.set()
            pump_thread.join()

    def test_rtd(self, opts: dict[str, Any]) -> None:
        # None is the chopped reading.
        polarities: list[int | None] = {"chop": [None], "both": [0, 1]}.get(
            opts["polarity"], [int(opts["polarity"])])
        channels = opts["channels"]
        readings: dict[tuple[int, int | None], list[float]] = {(c, p): [] for c in channels
                                                               for p in polarities}

        def label(p: int | None) -> str:
            return "chop" if p is None else f"p{p}"

        errors: dict[int, str] = {}

        def one_pass(_: int) -> None:
            for ch in channels:
                parts = []
                for p in polarities:
                    try:
                        ohms = self.sc.read_rtd(ch, p)
                    except SyncBoardError as exc:
                        message = exc.message if isinstance(exc, CommandError) else str(exc)
                        errors[ch] = message
                        parts.append(f"{label(p)} {message}")
                        continue
                    readings[(ch, p)].append(ohms)
                    parts.append(f"{label(p)} {ohms:9.3f} ohm ({pt1000_celsius(ohms):6.2f} C)")
                pair = [readings[(ch, p)][-1] for p in polarities if readings[(ch, p)]]
                if len(polarities) == 2 and len(pair) == 2:
                    parts.append(f"offset {(pair[1] - pair[0]) / 2 * 1000:+.1f} mohm")
                print(f"  ch{ch}: " + "  ".join(parts))

        repeat(opts["rounds"], 0.0, one_pass)

        if opts["rounds"] != 1:
            print("  Summary:")
            for ch in channels:
                for p in polarities:
                    if readings[(ch, p)]:
                        print(f"    ch{ch} {label(p)}: "
                              f"{describe(readings[(ch, p)], 'ohm', digits=3)}")
        expect = opts["expect"]
        if expect is None:
            for ch, message in errors.items():
                self.auto(f"RTD ch{ch} reads", False, message)
            if any(readings.values()):
                self.confirm("RTD readings", "Do the readings match what is connected?")
            return
        for ch in channels:
            values = [v for p in polarities for v in readings[(ch, p)]]
            worst = max((abs(v - expect) / expect for v in values), default=None)
            ok = worst is not None and worst * 100 <= opts["tol"] and ch not in errors
            detail = errors.get(ch) or (f"worst {worst * 100:.3f} % off {expect:g} ohm"
                                        if worst is not None else "no readings")
            self.auto(f"RTD ch{ch} = {expect:g} ohm", ok, detail)

    def power_sensors(self) -> None:
        """Sensors + fluxgate rails on for a reading test, if the PCA9685 is
        there to do it; the ADC inputs can be exercised either way."""
        if not self.have("pca9685"):
            print("  No PCA9685, so the sensor rails stay off (flex sensors unpowered); the "
                  "ADC inputs are still read.")
            return
        with self.step("sensor rail enables"):
            self.sc.set_enable(ScreenerEnable.SENSORS, True)
            self.sc.set_enable(ScreenerEnable.FLUXGATE, True)
            time.sleep(0.2)  # let the sensors power up

    @staticmethod
    def ask_volts(what: str) -> float | None:
        """A meter reading typed in by the operator; None if skipped."""
        while True:
            raw = input(f"  {what}, volts (Enter = skip): ").strip()
            if not raw:
                return None
            try:
                return float(raw)
            except ValueError:
                print("  A number in volts, please.")

    def settle_sweep(self, name: str, read: Callable[[float], tuple[float, ...]],
                     labels: tuple[str, ...], floors: tuple[float, ...],
                     settles: list[float], default_ms: float, rest_s: float) -> None:
        """Reads three times at each settle time, each after rest_s with the
        rail off, then checks the firmware's default settle is long enough:
        from there on every reading must agree with the longest settle's
        mean, within three standard deviations of those or the floor."""
        print(f"  {name} settle sweep, {rest_s:g} s with the rail off before each reading:")
        results: dict[float, list[tuple[float, ...]]] = {}
        for settle in settles:
            values: list[tuple[float, ...]] = []
            for _ in range(3):
                time.sleep(rest_s)
                try:
                    values.append(read(settle))
                except (SyncBoardError, ValueError) as exc:
                    message = exc.message if isinstance(exc, CommandError) else str(exc)
                    print(f"    {settle:>6g} ms: {message}")
                    break
            results[settle] = values
            if values:
                print(f"    {settle:>6g} ms: " + ";  ".join(
                    describe([v[i] for v in values], label, digits=4)
                    for i, label in enumerate(labels)))
        longest = settles[-1]
        if len(results[longest]) < 3:
            self.auto(f"{name} settle sweep", False, f"no clean readings at {longest:g} ms")
            return
        tolerances = []
        for i, floor in enumerate(floors):
            reference = [v[i] for v in results[longest]]
            tolerances.append((statistics.fmean(reference),
                               max(3 * statistics.stdev(reference), floor)))
        settled_from = longest
        for settle in reversed(settles):
            values = results[settle]
            if len(values) < 3 or any(abs(v[i] - mean) > tol for v in values
                                      for i, (mean, tol) in enumerate(tolerances)):
                break
            settled_from = settle
        within = ", ".join(f"{tol:.4g} {label}" for (_, tol), label in zip(tolerances, labels))
        self.auto(f"{name} default settle ({default_ms:g} ms) long enough",
                  settled_from <= default_ms, f"settled from {settled_from:g} ms, within {within}")

    def test_field(self, opts: dict[str, Any]) -> None:
        if not self.have("ads124s08"):
            self.field_by_meter()
            return
        which, settle, count = opts["which"], opts["settle_ms"], opts["count"]
        if "supply" in which:
            with self.step("FieldSensor supply"):
                supply = self.field.read_supply(settle)
                self.auto("flex 5 V at the FieldSensor", 4.5 <= supply <= 5.25, f"{supply:.3f} V")

        def hall() -> str:
            ratio = statistics.fmean(self.field.read_hall_ratios(count, settle))
            field_mt = (ratio - 0.5) * FieldSensorApi.HALL_MT_PER_RATIO
            beyond = " (beyond range)" if abs(field_mt) > FieldSensorApi.HALL_RANGE_MT else ""
            return f"{ratio:.5f} = {field_mt:+7.2f} mT{beyond}"

        def fluxgate() -> str:
            volts = statistics.fmean(self.field.read_fluxgate_volts(count, settle))
            beyond = " (at its rail)" if abs(volts) > FieldSensorApi.FLUXGATE_LIMIT_V else ""
            return f"{volts:+.5f} V = {volts / FieldSensorApi.FLUXGATE_V_PER_MT:+.4f} mT{beyond}"

        rtd_c: list[float] = []

        def rtd() -> str:
            ohms = self.field.read_rtd_ohms()
            rtd_c.append(pt1000_celsius(ohms))
            return f"{ohms:.2f} ohm = {rtd_c[-1]:.2f} C"

        # Sensors-rail readings first, the fluxgate (the heat source) last.
        readers = [(name, read) for name, read in
                   (("hall", hall), ("fluxgate", fluxgate), ("rtd", rtd)) if name in which]
        errors: dict[str, str] = {}
        good = False

        def one_round(_: int) -> None:
            nonlocal good
            parts = []
            for name, read in readers:
                try:
                    parts.append(f"{name} {read()}")
                    good = True
                except SyncBoardError as exc:
                    errors.setdefault(name, exc.message if isinstance(exc, CommandError)
                                      else str(exc))
                    parts.append(f"{name} err")
            print("  " + "   ".join(parts))

        if readers:
            repeat(opts["rounds"], 0.0, one_round)
        for name, message in errors.items():
            self.auto(f"FieldSensor {name} reads", False, message)
        if rtd_c:
            self.auto("RTD 7 at room temperature", 10.0 <= rtd_c[-1] <= 40.0, f"{rtd_c[-1]:.2f} C")
        if good:
            self.confirm("FieldSensor readings", "Plausible? (with no magnet near, Hall within "
                         "~3 mT of 0 and fluxgate within ~0.1 mT; a magnet moves both)")
            lit = " and ".join(led for led, used in (
                ("LL-4", {"supply", "hall", "rtd"} & set(which)), ("LL-3", "fluxgate" in which))
                if used)
            self.confirm("power per reading", f"{lit} lit only during the readings, and dark now?")
        if opts["sweep"]:
            with self.step("Hall settle sweep"):
                self.settle_sweep(
                    "Hall", lambda ms: (self.field.read_hall_mt(1, ms),), ("mT",), (0.2,),
                    [0, 1, 2, 5, 10, 20, 50, 100], FieldSensorApi.HALL_SETTLE_MS,
                    opts["rest_s"])
            with self.step("fluxgate settle sweep"):
                self.settle_sweep(
                    "fluxgate", lambda ms: (self.field.read_fluxgate_mt(1, ms),), ("mT",),
                    (0.002,), [0, 50, 100, 200, 300, 500, 700, 1000],
                    FieldSensorApi.FLUXGATE_SETTLE_MS, opts["rest_s"])
        if opts["heat_s"]:
            with self.step("fluxgate self-heating"):
                self.fluxgate_heating(opts["heat_s"])

    def fluxgate_heating(self, seconds: int) -> None:
        """Holds the fluxgate on for a while and then off for as long,
        reading the PT1000 on the same flex throughout."""
        print(f"  Fluxgate held on for {seconds} s, then off for {seconds} s; RTD 7:")
        start_c = self.field.read_temperature_c()
        peak_c = start_c
        for held in (True, False):
            self.sc.set_enable(ScreenerEnable.FLUXGATE, held)
            try:
                began = time.monotonic()
                while time.monotonic() - began < seconds:
                    celsius = self.field.read_temperature_c()
                    peak_c = max(peak_c, celsius)
                    print(f"    {'on ' if held else 'off'} {time.monotonic() - began:5.0f} s  "
                          f"{celsius:7.3f} C  ({celsius - start_c:+.3f})")
            finally:
                self.sc.set_enable(ScreenerEnable.FLUXGATE, False)
        print(f"  RTD 7 rose up to {peak_c - start_c:+.3f} C with the fluxgate held on.")

    def field_by_meter(self) -> None:
        print("  No SPI ADC, so the FieldSensor is read with a meter instead: on the ADC "
              "side of the conditioning, at the signal pad of each C8 cap (its other pad is "
              "GND). "
              "Both rails stay on until the end of this test.")
        self.sc.set_enable(ScreenerEnable.SENSORS, True)
        self.sc.set_enable(ScreenerEnable.FLUXGATE, True)
        try:
            ref_p = self.ask_volts("C8-8 (HallRefP = (HallP + 2.5 V) / 2, expect ~3.75)")
            ref_n = self.ask_volts("C8-9 (HallRefN = (HallN + 2.5 V) / 2, expect ~1.25)")
            if ref_p is not None and ref_n is not None:
                supply = 2 * (ref_p - ref_n)
                self.auto("flex 5 V at the FieldSensor", 4.5 <= supply <= 5.25, f"{supply:.3f} V")
                signal = self.ask_volts("C8-10 (HallSignal = (HallS + 2.5 V) / 2, expect ~2.5)")
                if signal is not None and supply > 0:
                    ratio = (signal - ref_n) / (ref_p - ref_n)
                    print(f"  Hall: {ratio:.4f} of its supply = "
                          f"{(ratio - 0.5) * FieldSensorApi.HALL_MT_PER_RATIO:+.2f} mT")
                    self.confirm("Hall sensor", "Within ~3 mT of 0 with no magnet near, and "
                                 "C8-10 moves with one (12.5 mV per mT at the cap)?")
            refout = self.ask_volts("C8-7 (FluxgateN = the DRV425's REFOUT, expect ~1.65)")
            if refout is not None:
                self.auto("fluxgate REFOUT", 1.4 <= refout <= 1.9, f"{refout:.3f} V")
                vout = self.ask_volts("C8-6 (FluxgateP = its VOUT)")
                if vout is not None:
                    print(f"  Fluxgate: VOUT - REFOUT {vout - refout:+.4f} V = "
                          f"{(vout - refout) / FieldSensorApi.FLUXGATE_V_PER_MT:+.4f} mT")
                    self.confirm("fluxgate", "Plausible (Earth's field is ~0.05 mT), and it "
                                 "moves with a magnet?")
        finally:
            self.sc.set_enable(ScreenerEnable.FLUXGATE, False)
            self.sc.set_enable(ScreenerEnable.SENSORS, False)
        self.confirm("rails off", "LL-3 and LL-4 off again?")

    def air_references(self, choice: str) -> list[AirReference]:
        if choice == "flex":
            return [AirReference.FLEX_SUPPLY]
        if choice == "offset":
            return [AirReference.OFFSET]
        if choice == "both":
            return [AirReference.FLEX_SUPPLY, AirReference.OFFSET]
        try:
            supply = self.field.read_supply()
        except SyncBoardError as exc:
            print(f"  FieldSensor supply unreadable ({exc}); reading against the offset.")
            return [AirReference.OFFSET]
        if supply < 4.5:
            print(f"  FieldSensor supply {supply:.2f} V (not connected?); reading against the "
                  "offset, which assumes the nominal 5 V.")
            return [AirReference.OFFSET]
        return [AirReference.FLEX_SUPPLY]

    def test_air(self, opts: dict[str, Any]) -> None:
        if not self.have("ads124s08"):
            self.air_by_meter()
            return
        references = self.air_references(opts["reference"])
        names = {AirReference.FLEX_SUPPLY: "flex", AirReference.OFFSET: "offset"}
        last: dict[AirReference, tuple[float, float]] = {}
        rtd_c: list[float] = []
        errors: dict[str, str] = {}

        def one_round(_: int) -> None:
            parts = []
            for ref in references:
                try:
                    reading = self.air.read_humidity(ref, opts["settle_ms"])
                    last[ref] = (reading.relative_humidity, reading.temperature_c)
                    parts.append(f"{names[ref]}: {reading.relative_humidity:5.1f} %RH "
                                 f"{reading.temperature_c:6.2f} C")
                except SyncBoardError as exc:
                    errors.setdefault(names[ref], exc.message if isinstance(exc, CommandError)
                                      else str(exc))
                    parts.append(f"{names[ref]}: err")
            if opts["rtd"]:
                try:
                    rtd_c.append(self.air.read_temperature_c())
                    parts.append(f"RTD 8 {rtd_c[-1]:6.2f} C")
                except SyncBoardError as exc:
                    errors.setdefault("RTD 8", exc.message if isinstance(exc, CommandError)
                                      else str(exc))
                    parts.append("RTD 8 err")
            print("  " + "   ".join(parts))

        repeat(opts["rounds"], 0.0, one_round)
        for name, message in errors.items():
            self.auto(f"AirSensor {name} reads", False, message)
        for ref, (rh, celsius) in last.items():
            self.auto(f"SHT40I RH ({names[ref]})", 0.0 <= rh <= 100.0, f"{rh:.1f} %RH")
            self.auto(f"SHT40I T ({names[ref]})", 10.0 <= celsius <= 40.0, f"{celsius:.2f} C")
            if rtd_c:
                self.auto(f"SHT40I T ({names[ref]}) agrees with RTD 8",
                          abs(celsius - rtd_c[-1]) <= opts["tol"],
                          f"{celsius - rtd_c[-1]:+.2f} C")
        if len(last) == 2:
            (rh_f, t_f), (rh_o, t_o) = last[AirReference.FLEX_SUPPLY], last[AirReference.OFFSET]
            print(f"  offset - flex: {rh_o - rh_f:+.2f} %RH, {t_o - t_f:+.2f} C (the error of "
                  "assuming the nominal 5 V supply and 2.5 V offset)")
        if last or rtd_c:
            self.confirm("power per reading", "LL-4 lit only during the readings, and dark now?")
        if opts["sweep"]:
            ref = references[0]

            def read(settle_ms: float) -> tuple[float, float]:
                reading = self.air.read_humidity(ref, settle_ms)
                return reading.relative_humidity, reading.temperature_c

            with self.step("humidity settle sweep"):
                self.settle_sweep(f"SHT40I ({names[ref]})", read, ("%RH", "C"), (0.5, 0.2),
                                  [0, 20, 50, 100, 200, 300, 500, 1000],
                                  AirSensorApi.HUMIDITY_SETTLE_MS, opts["rest_s"])

    def air_by_meter(self) -> None:
        print("  No SPI ADC, so the SHT40I is read with a meter instead, at the signal pad "
              "of C8-13 and C8-14 (the other pad is GND); this assumes the nominal 5 V "
              "supply. The sensors rail stays on until the end of this test.")
        self.sc.set_enable(ScreenerEnable.SENSORS, True)
        try:
            time.sleep(0.3)  # the SHT40I's outputs settle 200 ms after power-up
            readings = []
            for cap, label, expect, convert, unit in (
                    ("C8-13", "T_RH", "~2.2-2.35 at 18-28 C", sht40_celsius, "C"),
                    ("C8-14", "RH", "~2.1-2.7 at 30-60 %RH", sht40_rh_percent, "%RH")):
                volts = self.ask_volts(f"{cap} ({label} = (output + 2.5 V) / 2, {expect})")
                if volts is not None:
                    fraction = (2 * volts - AirSensorApi.OFFSET_V) / AirSensorApi.SUPPLY_V
                    readings.append(f"{label} {convert(fraction):.1f} {unit}")
            if readings:
                print("  SHT40I: " + ", ".join(readings))
                self.confirm("SHT40I", "Matches the room (to ~1 C and a few %RH)?")
        finally:
            self.sc.set_enable(ScreenerEnable.SENSORS, False)
        self.confirm("sensors rail off", "LL-4 off again?")

    def test_adc(self, opts: dict[str, Any]) -> None:
        if opts["sensors"]:
            self.power_sensors()
        ref = {"internal": ScreenerRef.INTERNAL, "rtd": ScreenerRef.RTD,
               "hall": ScreenerRef.HALL}[opts["ref"]]
        internal = ref is ScreenerRef.INTERNAL
        print(f"  AIN{opts['ainp']} ({AINPUTS[opts['ainp']]}) - AIN{opts['ainn']} "
              f"({AINPUTS[opts['ainn']]}), ref {opts['ref']}, gain {opts['gain']}")
        ratios: list[float] = []

        def one(_: int) -> None:
            r = self.sc.read_adc(opts["ainp"], opts["ainn"], ref, opts["gain"])
            ratios.append(r.ratio)
            volts = f"  {r.ratio * ScreenerApi.INTERNAL_REF_V:+.6f} V" if internal else ""
            print(f"  code {r.code:>9}  ratio {r.ratio:+.7f}{volts}")

        # A failing conversion ends the series (it would fail the same way
        # every time) but keeps the statistics of what was read.
        with self.step("ADS124S08 conversions"):
            repeat(opts["count"], 0.0, one)
        if ratios:
            print("  " + describe(ratios, "(ratio)", digits=7))
            if internal:
                print("  " + describe(ratios, "uV", scale=ScreenerApi.INTERNAL_REF_V * 1e6,
                                      digits=1))

    def test_displays(self, opts: dict[str, Any]) -> None:
        self.sc.set_enable(ScreenerEnable.SENSORS, True)
        if opts["rf"]:
            with self.step("RF detector enable"):
                self.sc.set_enable(ScreenerEnable.RF, True)
        self.confirm("displays powered", "Bar displays lit and steady (RF bars at their "
                     "no-signal level, B bars near zero)?")
        self.instruct("Bring a magnet up to the Hall sensor on the flex, then turn it round")
        self.confirm("B display", "Did the B bars move one way, then the other?")
        if opts["rf"]:
            self.confirm("RF displays", "With RF applied, do the RF bars respond? "
                         "(s if no RF source)")

    def test_pwm(self, opts: dict[str, Any]) -> None:
        ch = opts["channel"]
        pin = 5 + ch if ch <= 8 else 6 + ch  # LED0-7 are pins 6-13, LED8-15 pins 15-22
        print(f"  Channel {ch} drives {ScreenerApi.PWM_CHANNELS[ch]} (PWM1 pin {pin}).")
        self.sc.set_pwm(ch, opts["duty"])
        self.instruct("Measure now; Enter switches it back off")
        self.sc.set_pwm(ch, 0.0)


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
    bringup = ScreenerBringup(board)
    try:
        try:
            # What initialise() does, minus the setup: ensure_setup runs that
            # and reports which chips answered.
            board.disable()
            board.attach_led_board(False)
            board.attach_magnet_board(False)
            board.attach_screener_board(True)
        except CommandError as exc:
            if exc.command == "attachScreener":
                print(f"The firmware does not know the screener board ({exc.message}); "
                      "flash it with 'pio run -t upload'.")
                return 2
            raise
        bringup.ensure_setup()
        bringup.menu()
    finally:
        board.close()  # disables the system on the way out

    print("\n" + "=" * 64)
    width = max((len(step) for step, _ in bringup.results), default=0)
    for step, result in bringup.results:
        print(f"  {step:<{width}}  {result}")
    failed = [step for step, result in bringup.results if result == "FAIL"]
    print(f"\n{len(bringup.results)} checks: "
          f"{sum(1 for _, r in bringup.results if r == 'PASS')} passed, "
          f"{len(failed)} failed, "
          f"{sum(1 for _, r in bringup.results if r == 'SKIP')} skipped")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
