#!/usr/bin/env python3
"""Interactive bring-up for the screener board (ScreenerMobo) in the
SyncBoard's Expansion 2 header.

Pick a test from the menu, check or change its options, and run it. You come
back to the menu afterwards, so tests run in any order and can be repeated
with different options. Every test ends with all screener outputs off.
Ctrl+C stops the running test and returns to the menu; q quits and prints a
pass/fail summary.

Examples:
    python hwtest/screener_board.py
    python hwtest/screener_board.py --port /dev/cu.usbmodem1234

At the options prompt, press Enter to run, type name=value (several at once
are fine) to change options, e to step through them all, or b to go back.

Before running:
  - Screener board in Expansion 2 (SyncBoard H4), powered from its own 12 V
    supply on H2 (LL-5 lit).
  - SyncBoard flashed with firmware that has screener support (2.1.0+).
See docs/bringup/screener-board.md for what each test covers.
"""

from __future__ import annotations

import argparse
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
    CommandError,
    ScreenerChips,
    ScreenerEnable,
    ScreenerRef,
    SyncBoard,
    SyncBoardError,
    pt1000_celsius,
)
from syncboard.board import ScreenerApi  # noqa: E402

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
    needs_chip: str | None = None  # "ads7828" / "ads124s08": skipped if setup didn't find it
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

ENABLES = {
    "rf": ScreenerEnable.RF, "rf-spare": ScreenerEnable.RF_SPARE,
    "fluxgate": ScreenerEnable.FLUXGATE, "sensors": ScreenerEnable.SENSORS,
}


class ScreenerBringup(Checkout):

    def __init__(self, board: SyncBoard):
        super().__init__(board)
        self.sc = board.screener
        self.ready = False  # setupScreener has succeeded since the last enable
        self.chips: ScreenerChips | None = None  # what the last setup found
        self.tests = self.build_tests()

    # -- session plumbing ------------------------------------------------------

    def ensure_setup(self, quiet: bool = False) -> bool:
        if self.ready:
            return True
        try:
            if not self.board.status().enabled:
                self.board.enable()
            self.chips = self.sc.setup()
        except SyncBoardError as exc:
            if not quiet:
                print(f"  Screener setup failed: {exc}")
                print("  Is the board in Expansion 2 and powered (12 V on H2, LL-5 lit)?"
                      " The 'link' test walks through this.")
            return False
        self.ready = True
        return True

    def outputs_off(self) -> None:
        if not self.ready:
            return  # nothing can be on
        try:
            self.sc.all_off()
        except SyncBoardError as exc:
            print(f"  Could not switch the screener outputs off: {exc}")
            self.ready = False

    def run_test(self, test: Test) -> None:
        values = test.values()
        start = len(self.results)
        outcome = None
        self.banner(f"{test.key}: {test.title}")
        try:
            if test.needs_setup and not self.ensure_setup():
                test.last = "not ready"
                return
            if test.needs_chip and self.chips and not getattr(self.chips, test.needs_chip):
                print(f"  Skipped: the {test.needs_chip.upper()} did not answer at setup.")
                test.last = f"no {test.needs_chip.upper()}"
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
                "Runs setupScreener, which needs the PCA9685 (0x51) and probes the ADS7828 "
                "(0x4B) and ADS124S08 (SPI through Iso-2, pattern write/read-back), then "
                "reads the ADS124S08's own supply monitors: AVDD is the board's "
                "heartbeat-gated 5 V analog rail (VReg-3), DVDD its always-on 3.3 V "
                "(VReg-5). Set expect_spi=no on a board without the SPI ADC."),
                self.test_chips, needs_setup=False, options=[
                    Option("tol", "5", "% allowed on the supply readings", float_in(0.1, 50)),
                    Option("expect_spi", "yes", "the ADS124S08 is fitted and should answer",
                           parse_bool),
                ]),
            Test("enables", "Rail and detector enables", (
                "Turns each enable on and off in turn and asks about its indicator: LL-2 "
                "RF detectors 1-3, LL-3 fluxgate rail, LL-4 sensors rail (flex sensors, "
                "LED displays, RTD muxes). The spare RF detector's enable has no "
                "indicator; probe its ENBL pin."),
                self.test_enables, options=[
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
                self.test_switches, options=[
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
                self.test_rf_adc, needs_chip="ads7828", options=[
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
                "Readings are sign-corrected for the excitation polarity, so both "
                "polarities should agree; a negative value means the V pair is swapped "
                "against the D pair. A 1 kOhm 0.1 % resistor wired 4-wire makes a good "
                "dummy (0 C on a PT1000). Turns the sensors enable on (it powers the muxes)."),
                self.test_rtd, needs_chip="ads124s08", options=[
                    Option("channels", "1-8", "RTD channels, e.g. 1-6 or 3,7",
                           channel_list(1, ScreenerApi.NUM_RTDS)),
                    Option("polarity", "both", "excitation polarity: 0, 1 or both",
                           one_of("0", "1", "both")),
                    Option("rounds", "1", "passes over the channels (0 = until Ctrl+C)",
                           int_in(0, 10000)),
                    Option("expect", "-", "ohms of a known resistor on every channel (- = none)",
                           optional_float),
                    Option("tol", "0.1", "% allowed from 'expect'", float_in(0.001, 100)),
                ]),
            Test("sensors", "Flex sensors: fluxgate, Hall, humidity", (
                "Read as the sheet's ADC setup note describes: fluxgate AIN2-AIN3 against "
                "the internal 2.5 V reference; Hall AIN4-AIN5 against its own excitation "
                "(AIN6/AIN7), as the fraction (HallS - 2.5 V)/(HallP - HallN); the humidity "
                "sensor's RH (AIN9) and T_RH (AIN8) against the 2.5 V offset, converted back "
                "to the sensor's own output voltage. Turns the sensors and fluxgate enables "
                "on for the test."),
                self.test_sensors, needs_chip="ads124s08", options=[
                    Option("which", "all", "fluxgate, hall, rh, t_rh (comma list) or all",
                           names_from("fluxgate", "hall", "rh", "t_rh")),
                    Option("rounds", "5", "rounds of readings (0 = until Ctrl+C)", int_in(0, 10000)),
                    Option("interval", "0.5", "seconds between rounds", float_in(0.0, 60.0)),
                ]),
            Test("adc", "Any ADS124S08 input pair", (
                "Repeated conversions of one input pair with statistics. Inputs: "
                + "; ".join(f"{k} {v}" for k, v in AINPUTS.items())
                + ". ref is internal (2.5 V), rtd (REFP0/REFN0) or hall (AIN6/AIN7). "
                "Gain 1 bypasses the PGA; above that the inputs need ~0.15 V of headroom "
                "from the rails."),
                self.test_adc, needs_chip="ads124s08", options=[
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
                self.test_displays, options=[
                    Option("rf", "yes", "also enable the RF detectors, so the RF bars are live",
                           parse_bool),
                ]),
            Test("pwm", "Raw PCA9685 channel", (
                "Drives one PCA9685 output directly, for probing. Channels: "
                + "; ".join(f"{c} {n}" for c, n in ScreenerApi.PWM_CHANNELS.items())
                + ". It is switched back off when you press Enter."),
                self.test_pwm, options=[
                    Option("channel", "3", "PCA9685 channel 1-16", int_in(1, 16)),
                    Option("duty", "1.0", "0..1 (fractional = ~200 Hz PWM)", float_in(0.0, 1.0)),
                ]),
        ]

    def test_link(self, opts: dict[str, Any]) -> None:
        self.confirm("12 V input", "LL-5 (12 V) lit?")
        if not self.board.status().enabled:
            self.board.enable()
        self.confirm("heartbeat up", "LL-6 (ENWD, the isolated heartbeat) lit? "
                     "(N1 pin 2 ~3.3 V, PWM1 pin 23 (OE) ~0 V)")
        addresses = self.board.scan_i2c()
        print(f"  I2C devices: {[hex(a) for a in addresses]}")
        self.auto("PCA9685 at 0x51", 0x51 in addresses)
        self.auto("ADS7828 at 0x4B", 0x4B in addresses)
        if opts["interlock"]:
            print("  Disabling: the heartbeat stops, so ENWD should drop...")
            self.ready = False
            self.board.disable()
            self.confirm("interlock", "LL-6 off, and PWM1 pin 23 (OE) back at ~3.3 V?")
            self.board.enable()
        self.ready = False
        self.auto("setupScreener", self.ensure_setup())

    def test_chips(self, opts: dict[str, Any]) -> None:
        self.ready = False
        ok = self.ensure_setup(quiet=True)
        detail = ""
        if not ok:
            try:
                self.sc.setup()
            except SyncBoardError as exc:
                detail = str(exc)
        self.auto("setupScreener (PCA9685 at 0x51)", ok, detail)
        if not ok or self.chips is None:
            return
        self.auto("ADS7828 at 0x4B", self.chips.ads7828)
        if self.chips.ads7828:
            readings = [self.sc.read_rf(ch) for ch in ScreenerApi.RF_CHANNELS]
            print("  RF ADC: " + ", ".join(f"{v:.3f}" for v in readings) + " V")
        if not opts["expect_spi"]:
            print(f"  ADS124S08 {'answered' if self.chips.ads124s08 else 'absent'} (not checked)")
            return
        self.auto("ADS124S08 on SPI", self.chips.ads124s08)
        if not self.chips.ads124s08:
            return
        tol = opts["tol"] / 100
        for analog, name, nominal in ((True, "AVDD (5 V analog rail)", 5.0),
                                      (False, "DVDD (3.3 V digital)", 3.3)):
            volts = self.sc.read_supply(analog)
            self.auto(f"ADS124S08 {name}", abs(volts - nominal) <= tol * nominal,
                      f"{volts:.3f} V")

    def test_enables(self, opts: dict[str, Any]) -> None:
        indicator = {"rf": "LL-2", "fluxgate": "LL-3", "sensors": "LL-4"}
        rf_watch = {"rf": (1, 4, 7), "rf-spare": (8,)}
        rail = {"fluxgate": "Con_S pin 3 (3V3_Fluxgate_Con, or CF-5's connector side) ~3.3 V",
                "sensors": "Con_S pin 6 (5V_Sensors_Con, or CF-6's connector side) ~5 V"}
        for name in opts["which"]:
            watch = rf_watch.get(name, ())
            before = [self.sc.read_rf(ch) for ch in watch]
            self.sc.set_enable(ENABLES[name], True)
            if watch:
                time.sleep(0.1)
                after = [self.sc.read_rf(ch) for ch in watch]
                print("  " + ", ".join(f"{ScreenerApi.RF_CHANNELS[ch]} {b:.3f} -> {a:.3f} V"
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
            if opts["mode"] == "steady":
                self.sc.set_switch(ch, 1.0)
                self.confirm(f"switch {ch} on", f"Channel {ch}: LL-1 lit and HP-1 pin 2 (VL) "
                             "pulled to ~0 V?")
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
            self.sc.set_enable(ScreenerEnable.RF, True)
            self.sc.set_enable(ScreenerEnable.RF_SPARE, True)
            time.sleep(0.1)
        names = ScreenerApi.RF_CHANNELS
        print("  " + " ".join(f"{n:>8}" for n in names.values()))
        last: dict[int, float] = {}

        def one_round(_: int) -> None:
            last.update({ch: self.sc.read_rf(ch) for ch in names})
            print("  " + " ".join(f"{last[ch]:>8.3f}" for ch in names))

        repeat(opts["rounds"], opts["interval"], one_round)
        if last:
            # Nominal AD8302 transfer functions from the datasheet.
            print(f"  Nominal: RF12 {(last[2] - 0.9) / 0.03:+.1f} dB, {180 - last[3] / 0.01:.0f} deg;"
                  f"  RF23 {(last[5] - 0.9) / 0.03:+.1f} dB, {180 - last[6] / 0.01:.0f} deg")
        self.confirm("RF detectors", "Readings plausible, and (if you applied RF) did the "
                     "matching channels respond?")

    def test_rf_switch(self, opts: dict[str, Any]) -> None:
        self.instruct("SyncBoard J1-22 on pins 1-2 (expansion), meter or scope on the RFS1 "
                      "centre pin")
        if opts["supply"]:
            self.confirm("RF switch supply", "HP-4 pin 2 ~+5 V and pin 1 ~-5 V?")
        if opts["mode"] == "steady":
            self.sc.set_rf_switch(True)
            self.confirm("RF switch control high", "RFS1 reads ~5 V?")
            self.sc.set_rf_switch(False)
            self.confirm("RF switch control low", "RFS1 reads ~0 V?")
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
        self.sc.set_enable(ScreenerEnable.SENSORS, True)
        polarities = [0, 1] if opts["polarity"] == "both" else [int(opts["polarity"])]
        channels = opts["channels"]
        readings: dict[tuple[int, int], list[float]] = {(c, p): [] for c in channels
                                                        for p in polarities}
        errors: dict[int, str] = {}

        def one_pass(_: int) -> None:
            for ch in channels:
                parts = []
                for p in polarities:
                    try:
                        ohms = self.sc.read_rtd(ch, p)
                    except CommandError as exc:
                        errors[ch] = exc.message
                        parts.append(f"p{p} {exc.message}")
                        continue
                    readings[(ch, p)].append(ohms)
                    parts.append(f"p{p} {ohms:9.3f} ohm ({pt1000_celsius(ohms):6.2f} C)")
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
                        print(f"    ch{ch} p{p}: {describe(readings[(ch, p)], 'ohm', digits=3)}")
        expect = opts["expect"]
        if expect is None:
            self.confirm("RTD readings", "Do the readings match what is connected?")
            return
        for ch in channels:
            values = [v for p in polarities for v in readings[(ch, p)]]
            worst = max((abs(v - expect) / expect for v in values), default=None)
            ok = worst is not None and worst * 100 <= opts["tol"] and ch not in errors
            detail = errors.get(ch) or (f"worst {worst * 100:.3f} % off {expect:g} ohm"
                                        if worst is not None else "no readings")
            self.auto(f"RTD ch{ch} = {expect:g} ohm", ok, detail)

    def test_sensors(self, opts: dict[str, Any]) -> None:
        self.sc.set_enable(ScreenerEnable.SENSORS, True)
        self.sc.set_enable(ScreenerEnable.FLUXGATE, True)
        time.sleep(0.2)  # let the sensors power up
        readers = {
            "fluxgate": ("fluxgate V", self.sc.read_fluxgate, "{:+.5f}"),
            "hall": ("Hall ratio", self.sc.read_hall, "{:+.6f}"),
            "rh": ("RH V", self.sc.read_humidity, "{:.4f}"),
            "t_rh": ("T_RH V", self.sc.read_humidity_temperature, "{:.4f}"),
        }
        chosen = [readers[name] for name in opts["which"]]
        print("  " + " ".join(f"{label:>12}" for label, _, _ in chosen))
        repeat(opts["rounds"], opts["interval"], lambda _: print(
            "  " + " ".join(f"{fmt.format(read()):>12}" for _, read, fmt in chosen)))
        self.confirm("flex sensors", "Plausible? (a magnet near the Hall sensor or fluxgate "
                     "should move them)")

    def test_adc(self, opts: dict[str, Any]) -> None:
        if opts["sensors"]:
            self.sc.set_enable(ScreenerEnable.SENSORS, True)
            self.sc.set_enable(ScreenerEnable.FLUXGATE, True)
            time.sleep(0.2)
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

        repeat(opts["count"], 0.0, one)
        if ratios:
            print("  " + describe(ratios, "(ratio)", digits=7))
            if internal:
                print("  " + describe(ratios, "uV", scale=ScreenerApi.INTERNAL_REF_V * 1e6,
                                      digits=1))

    def test_displays(self, opts: dict[str, Any]) -> None:
        self.sc.set_enable(ScreenerEnable.SENSORS, True)
        if opts["rf"]:
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
            board.initialise(screener_board=True)
            bringup.ready = False
            bringup.ensure_setup()  # records which ADCs answered
        except CommandError as exc:
            if exc.command == "attachScreener":
                print(f"The firmware does not know the screener board ({exc.message}); "
                      "flash it with 'pio run -t upload'.")
                return 2
            print(f"Screener setup failed: {exc.message}")
            print("Continuing: the 'link' and 'chips' tests help find out why.")
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
