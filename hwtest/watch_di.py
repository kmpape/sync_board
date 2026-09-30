#!/usr/bin/env python3
"""Continuously prints the value read on one digital input.

Leave it running while you move the D_IN connection between a voltage
source and GND, and watch the reading follow. Ctrl+C to stop.

Examples:
    python hwtest/watch_di.py                    # D_IN_1, 5 reads per second
    python hwtest/watch_di.py --channel 3 --interval 0.5
"""

from __future__ import annotations

import argparse
import pathlib
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))

from syncboard import SyncBoard, SyncBoardError  # noqa: E402


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", help="serial port (default: auto-discover)")
    parser.add_argument("--channel", type=int, default=1, choices=range(1, 5),
                        help="digital input to read, 1-4 (default: 1)")
    parser.add_argument("--interval", type=float, default=0.2,
                        help="seconds between reads (default: 0.2)")
    args = parser.parse_args()

    print("Connecting...")
    try:
        board = SyncBoard.connect(args.port)
    except SyncBoardError as exc:
        print(f"Could not connect: {exc}")
        return 2
    try:
        board.initialise()
        print(f"Reading D_IN_{args.channel} every {args.interval} s (Ctrl+C to stop)")
        start = time.monotonic()
        while True:
            high = board.io.read_di(args.channel)
            print(f"  {time.monotonic() - start:7.1f} s  D_IN_{args.channel} = "
                  f"{'1 (high)' if high else '0 (low)'}")
            time.sleep(args.interval)
    except KeyboardInterrupt:
        print()
    finally:
        board.close()  # disables the system on the way out
    return 0


if __name__ == "__main__":
    sys.exit(main())
