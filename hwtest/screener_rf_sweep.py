#!/usr/bin/env python3
"""Frequency sweep of the screener board's RF 1 input with a Windfreak
SynthNV Pro.

Steps the generator from 200 to 800 MHz at -10 dBm and, at each frequency,
reads RF1_Mag: the AD8318 detector on Con1, behind the input bandpass filter
and splitter, on ADS7828 channel 1. The result is the RF 1 path's response
across the band. Readings go to the terminal and a CSV file.

Connect the SynthNV Pro's RF output to Con1 (RF 1, bore pickup) on the
screener board. The board's inputs take 4 dBm at most.

The AD8318 output falls as power rises (nominally -24.5 mV/dB, 20 dBm
intercept), so a dBm estimate at the detector is printed alongside the
volts. It is nominal: the slope is good for relative levels (e.g. the
filter's -3 dB band), the absolute value is not calibrated. The ADS7828
reads at most 1.8 V, which the AD8318 passes below about -53 dBm, so
strongly rejected frequencies read as "full scale".

Examples:
    python hwtest/screener_rf_sweep.py
    python hwtest/screener_rf_sweep.py --step 1 --average 16 --out rf1.csv
    python hwtest/screener_rf_sweep.py --channel 2     # AD8302 #1 gain (Con1 vs Con2)

Needs the SynthNVProDriver package next to this repository
(../SynthNVProDriver) and screener support in the SyncBoard firmware.
"""

from __future__ import annotations

import argparse
import csv
import logging
import pathlib
import statistics
import sys
import time

import serial.tools.list_ports

REPO = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO))
sys.path.insert(0, str(REPO.parent))  # for the SynthNVProDriver package

from SynthNVProDriver.nvprocontroller import RF_MUTE, SynthNVProController  # noqa: E402
from syncboard import CommandError, ScreenerEnable, SyncBoard, SyncBoardError  # noqa: E402
from syncboard.board import ScreenerApi  # noqa: E402

MAX_INPUT_DBM = 4.0         # "MAX INPUT POWER=4dbm" on the ScreenerMobo sheet
ADC_FULL_SCALE_V = 1.795    # ADS7828 at 1.8 V reference, less one LSB and a bit
AD8318_SLOPE_V_PER_DB = -0.0245
AD8318_INTERCEPT_DBM = 20.0

# Which detector enable each ADS7828 channel needs (AD8302 channels need none).
CHANNEL_ENABLE = {1: ScreenerEnable.RF, 4: ScreenerEnable.RF, 7: ScreenerEnable.RF,
                  8: ScreenerEnable.RF_SPARE}


def find_windfreak_port() -> str:
    for port in serial.tools.list_ports.comports():
        if "SynthNV" in (port.description or "") or "VID:PID=0483:A3E7" in port.hwid.upper():
            return port.device
    raise SystemExit("No SynthNV Pro found on USB; pass --windfreak-port.")


def nominal_detector_dbm(volts: float) -> float:
    return AD8318_INTERCEPT_DBM + volts / AD8318_SLOPE_V_PER_DB


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--start", type=float, default=200.0, help="start frequency, MHz (default 200)")
    parser.add_argument("--stop", type=float, default=800.0, help="stop frequency, MHz (default 800)")
    parser.add_argument("--step", type=float, default=5.0, help="frequency step, MHz (default 5)")
    parser.add_argument("--power", type=float, default=-10.0,
                        help="generator power, dBm (default -10; the board takes 4 dBm at most)")
    parser.add_argument("--settle-ms", type=float, default=50.0,
                        help="wait after each frequency change before reading (default 50)")
    parser.add_argument("--average", type=int, default=4,
                        help="ADC readings averaged per point (default 4)")
    parser.add_argument("--channel", type=int, default=1, choices=range(1, 9),
                        help="ADS7828 channel to read (default 1 = RF1_Mag)")
    parser.add_argument("--out", type=pathlib.Path,
                        help="CSV file (default rf<channel>_sweep_<date-time>.csv here)")
    parser.add_argument("--port", help="SyncBoard serial port (default: auto-discover)")
    parser.add_argument("--windfreak-port", help="SynthNV Pro serial port (default: auto-discover)")
    args = parser.parse_args()

    if not 12.5 <= args.start < args.stop <= 6400.0:
        parser.error("need 12.5 <= start < stop <= 6400 MHz (the SynthNV Pro's range)")
    if args.step <= 0:
        parser.error("--step must be positive")
    if not -60.0 <= args.power <= MAX_INPUT_DBM:
        parser.error(f"--power must be -60..{MAX_INPUT_DBM:g} dBm (the board's input limit)")
    if args.average < 1:
        parser.error("--average must be at least 1")
    count = int((args.stop - args.start) / args.step + 1e-9) + 1
    freqs = [args.start + i * args.step for i in range(count)]
    channel = args.channel
    name = ScreenerApi.RF_CHANNELS[channel]
    is_ad8318 = channel in CHANNEL_ENABLE
    out = args.out or pathlib.Path(f"rf{channel}_sweep_{time.strftime('%Y%m%d-%H%M%S')}.csv")

    logging.basicConfig(level=logging.INFO, format="  [%(levelname)s] %(message)s")

    print("Connecting to the SyncBoard...")
    try:
        board = SyncBoard.connect(args.port)
    except SyncBoardError as exc:
        print(f"Could not connect: {exc}")
        return 2
    rows: list[dict] = []
    nv = None
    try:
        try:
            board.disable()
            board.attach_screener_board(True)
        except CommandError as exc:
            print(f"The firmware does not know the screener board ({exc.message}); "
                  "flash it with 'pio run -t upload'.")
            return 2
        board.enable()
        chips = board.screener.setup()
        if not chips.ads7828:
            print("The screener's ADS7828 did not answer, so RF 1 cannot be read.")
            return 1
        if is_ad8318:
            if not chips.pca9685:
                print("The screener's PCA9685 did not answer, so the AD8318 detectors cannot "
                      "be enabled and their readings would mean nothing.")
                return 1
            board.screener.set_enable(CHANNEL_ENABLE[channel], True)

        port = args.windfreak_port or find_windfreak_port()
        print(f"Connecting to the SynthNV Pro on {port}...")
        nv = SynthNVProController.from_serial_port(port)
        print(f"  {nv.show_model_type()}")
        nv.set_rf_frequency(freqs[0])
        nv.set_rf_power(args.power)
        nv.set_rf_mute(RF_MUTE.NOT_MUTED)
        nv.set_pll_enable(True)  # RF output on
        try:
            print(f"  output {nv.get_rf_power():.3f} dBm")
        except ValueError:
            print("  (could not read back the output power setting)")

        print(f"\nSweeping {args.start:g}-{args.stop:g} MHz in {args.step:g} MHz steps "
              f"({count} points) at {args.power:g} dBm, reading {name} (channel {channel}).")
        print("Ctrl+C stops early; what was measured is still saved.\n")
        print(f"  {'MHz':>9}  {'volts':>7}  {'std mV':>6}" + ("  detector dBm (nominal)" if is_ad8318 else ""))
        for f in freqs:
            nv.set_rf_frequency(f)
            time.sleep(args.settle_ms / 1000)
            readings = []
            for _ in range(args.average):
                try:
                    readings.append(board.screener.read_rf(channel))
                except SyncBoardError as exc:  # one bad read does not end the sweep
                    print(f"  {f:9.3f}  read failed: {exc}")
            if not readings:
                continue
            volts = statistics.fmean(readings)
            spread = statistics.stdev(readings) if len(readings) > 1 else 0.0
            saturated = volts >= ADC_FULL_SCALE_V
            dbm = nominal_detector_dbm(volts) if is_ad8318 else None
            rows.append({"frequency_mhz": f, "set_power_dbm": args.power, "volts": volts,
                         "volts_std": spread, "readings": len(readings),
                         "detector_dbm_nominal": "" if dbm is None else round(dbm, 2),
                         "adc_full_scale": int(saturated)})
            text = f"  {f:9.3f}  {volts:7.4f}  {spread * 1000:6.1f}"
            if dbm is not None:
                text += "  full scale (below ~-53)" if saturated else f"  {dbm:+7.1f}"
            print(text)
    except KeyboardInterrupt:
        print("\n  Stopped early.")
    finally:
        if nv is not None:
            try:
                nv.set_pll_enable(False)  # RF output off
            except Exception as exc:  # noqa: BLE001 - best effort on the way out
                print(f"  Could not switch the SynthNV Pro output off: {exc}")
            nv.connection.disconnect()
        board.close()  # disables the system: the detector enables drop

    if not rows:
        print("No data recorded.")
        return 1
    with out.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    print(f"\n{len(rows)} points saved to {out}")

    if is_ad8318:
        # The strongest response is the lowest detector voltage; the band is
        # where the nominal level is within 3 dB of it (slope only, so the
        # uncalibrated intercept cancels).
        in_range = [r for r in rows if not r["adc_full_scale"]]
        if not in_range:
            print("Every point read at ADC full scale: no signal reached the detector.")
            return 0
        peak = min(in_range, key=lambda r: r["volts"])
        band = [r["frequency_mhz"] for r in in_range
                if r["detector_dbm_nominal"] >= peak["detector_dbm_nominal"] - 3.0]
        print(f"Strongest at {peak['frequency_mhz']:g} MHz: {peak['volts']:.4f} V "
              f"(~{peak['detector_dbm_nominal']:+.1f} dBm nominal at the detector)")
        print(f"Within 3 dB of that: {min(band):g}-{max(band):g} MHz")
    return 0


if __name__ == "__main__":
    sys.exit(main())
