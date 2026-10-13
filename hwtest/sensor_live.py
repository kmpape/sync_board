#!/usr/bin/env python3
"""Live plot of the screener's sensor boards: fluxgate and Hall field,
the three temperatures (FieldSensor PT1000, AirSensor PT1000, SHT40I) and
the SHT40I humidity.

A background thread reads every sensor in turn and the plot redraws from
what it has. Each reading is independent: one that fails (a missing chip,
the open sensors rail, a field out of range) is plotted as a gap, its error
is shown in the panel title and logged once per new message, and the loop
carries on. Each fluxgate reading powers the DRV425 only for that reading
(500 ms settle), so --interval also sets how hard the head is heated.

    uv run --group plot python hwtest/sensor_live.py
    uv run --group plot python hwtest/sensor_live.py --interval 5 --window 600

Close the window or press Ctrl+C to stop; the board is disabled on exit.
--csv saves every reading as it arrives.
"""

from __future__ import annotations

import argparse
import collections
import csv
import math
import pathlib
import sys
import threading
import time

import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))

from syncboard import AirReference, SyncBoard, SyncBoardError  # noqa: E402

# (key, label, panel); panels are drawn top to bottom in this order.
SERIES = [
    ("fluxgate_mt", "Fluxgate", "Field (mT)"),
    ("hall_mt", "Hall", "Field (mT)"),
    ("field_rtd_c", "FieldSensor PT1000", "Temperature (°C)"),
    ("air_rtd_c", "AirSensor PT1000", "Temperature (°C)"),
    ("sht_c", "SHT40I", "Temperature (°C)"),
    ("rh_pct", "SHT40I", "Humidity (%RH)"),
]
PANELS = list(dict.fromkeys(panel for _, _, panel in SERIES))


class Reader(threading.Thread):
    """Reads all sensors in a loop into per-series deques of (t, value)."""

    def __init__(self, board: SyncBoard, args: argparse.Namespace):
        super().__init__(daemon=True)
        self.board = board
        self.args = args
        self.stop = threading.Event()
        self.lock = threading.Lock()
        self.t0 = time.monotonic()
        self.data = {key: collections.deque() for key, _, _ in SERIES}
        self.errors: dict[str, str] = {}
        self.csv = None
        if args.csv:
            self._csv_file = open(args.csv, "w", newline="")
            self.csv = csv.writer(self._csv_file)
            self.csv.writerow(["t_s", "series", "value", "error"])

    def _record(self, key: str, value: float, error: str = "") -> None:
        t = time.monotonic() - self.t0
        with self.lock:
            series = self.data[key]
            series.append((t, value))
            while series and series[0][0] < t - self.args.window:
                series.popleft()
            if error and self.errors.get(key) != error:
                print(f"  [{t:7.1f} s] {key}: {error}")
            self.errors[key] = error
        if self.csv:
            self.csv.writerow([f"{t:.3f}", key, value, error])
            self._csv_file.flush()

    def _read(self, keys: tuple[str, ...], fn) -> None:
        try:
            values = fn()
            if not isinstance(values, tuple):
                values = (values,)
            for key, value in zip(keys, values):
                self._record(key, value)
        except (SyncBoardError, ValueError) as exc:
            for key in keys:
                self._record(key, math.nan, str(exc))

    def run(self) -> None:
        field = self.board.screener.field_sensor
        air = self.board.screener.air_sensor
        reference = AirReference[self.args.humidity_reference]
        count = self.args.count

        def humidity():
            reading = air.read_humidity(reference)
            return reading.temperature_c, reading.relative_humidity

        while not self.stop.is_set():
            start = time.monotonic()
            self._read(("fluxgate_mt",), lambda: field.read_fluxgate_mt(count))
            self._read(("hall_mt",), lambda: field.read_hall_mt(count))
            if not self.args.no_rtd:
                self._read(("field_rtd_c",), field.read_temperature_c)
                self._read(("air_rtd_c",), air.read_temperature_c)
            self._read(("sht_c", "rh_pct"), humidity)
            self.stop.wait(max(0.0, self.args.interval - (time.monotonic() - start)))
        if self.csv:
            self._csv_file.close()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", help="serial port (default: auto-discover)")
    parser.add_argument("--interval", type=float, default=0.0,
                        help="minimum seconds per sweep of all sensors (default: as fast "
                             "as they read, ~4 s)")
    parser.add_argument("--window", type=float, default=300.0,
                        help="seconds of history shown (default 300)")
    parser.add_argument("--count", type=int, default=4,
                        help="conversions averaged per field reading, 1..16 (default 4)")
    parser.add_argument("--humidity-reference", choices=[r.name for r in AirReference],
                        default=AirReference.FLEX_SUPPLY.name,
                        help="SHT40I reference (OFFSET works without the FieldSensor)")
    parser.add_argument("--no-rtd", action="store_true",
                        help="skip the PT1000s (~1.1 s each) for a faster field update")
    parser.add_argument("--csv", type=pathlib.Path, help="also log readings to this CSV")
    args = parser.parse_args()

    print("Connecting...")
    try:
        board = SyncBoard.connect(args.port)
    except SyncBoardError as exc:
        print(f"Could not connect: {exc}")
        return 2

    try:
        board.initialise(screener_board=True)
        chips = board.screener.setup()
        print(f"Screener chips: {chips}")
        if not chips.ads124s08 or not chips.pca9685:
            print("  The sensor readings need the ADS124S08 and PCA9685; missing ones "
                  "will show as errors.")

        reader = Reader(board, args)
        fig, axes = plt.subplots(len(PANELS), 1, sharex=True, figsize=(10, 9))
        fig.canvas.manager.set_window_title("Screener sensors")
        lines = {}
        for ax, panel in zip(axes, PANELS):
            ax.set_ylabel(panel)
            ax.grid(True, alpha=0.3)
            for key, label, p in SERIES:
                if p == panel:
                    (lines[key],) = ax.plot([], [], ".-", ms=3, lw=1, label=label)
            ax.legend(loc="upper left", fontsize=8)
        axes[-1].set_xlabel("Time (s)")
        fig.tight_layout()

        def update(_frame):
            with reader.lock:
                snapshot = {k: list(v) for k, v in reader.data.items()}
                errors = dict(reader.errors)
            for key, points in snapshot.items():
                lines[key].set_data([p[0] for p in points], [p[1] for p in points])
            for ax, panel in zip(axes, PANELS):
                keys = [k for k, _, p in SERIES if p == panel]
                latest = []
                for key in keys:
                    label = lines[key].get_label()
                    good = [v for _, v in snapshot[key] if not math.isnan(v)]
                    if errors.get(key):
                        latest.append(f"{label}: error")
                    elif good:
                        latest.append(f"{label}: {good[-1]:.4g}")
                ax.set_title("   ".join(latest), fontsize=9, loc="right")
                ax.relim()
                ax.autoscale_view()
            t_now = time.monotonic() - reader.t0
            axes[-1].set_xlim(max(0.0, t_now - args.window), max(t_now, 10.0))
            return list(lines.values())

        anim = FuncAnimation(fig, update, interval=500, cache_frame_data=False)  # noqa: F841
        reader.start()
        try:
            plt.show()
        except KeyboardInterrupt:
            pass
        reader.stop.set()
        reader.join(timeout=5)
    finally:
        board.close()  # disables the system, which turns the rails off
    return 0


if __name__ == "__main__":
    sys.exit(main())
