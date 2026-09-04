# SyncBoard

Firmware (Teensy 4.1) and Python client for the microscope "SyncBoard": LED
illumination, magnet control, camera-synchronised imaging, timed analog/digital
signal sequences, and general bench IO.

## Repository layout

```
src/            firmware (PlatformIO, Arduino framework)
syncboard/      Python client package
tests/          unit tests for the client (no hardware needed)
hwtest/         interactive hardware checkout script (bench + scope)
```

## Firmware

Build and flash with [PlatformIO](https://platformio.org/) (`pipx install
platformio` or `uv tool install platformio`):

```sh
pio run              # compile
pio run -t upload    # flash the connected Teensy
```

Firmware modules:

| Module        | Responsibility |
|---------------|----------------|
| `Protocol`    | serial line protocol: parsing, replies, faults, logging |
| `main.cpp`    | command table + dispatch, main loop |
| `System`      | enable/disable orchestration, soft reset |
| `Io`          | GPIOs, digital in/out, ADC/DAC access, 12 V switches, bus bring-up |
| `Chips`       | register-level drivers (PCA9685, ADS7828, AD5668, AD5669) |
| `Leds`        | LED board: calibration, levels, timed switching, measurement |
| `Magnet`      | magnet board: current/field control, Hall sensors, calibration |
| `Signals`     | timed sequences (AWG output, ADC recording, conductor/slave sync) |
| `Imaging`     | camera-synchronised image sequences |
| `Calibration` | versioned EEPROM storage for LED calibrations |

## Wire protocol (v2)

One ASCII line per message, `\n`-terminated, `/`-separated fields:

```
request : $<tag>/<command>[/<arg>...]        e.g.  $17/setDac/3/1.25
reply   : $<tag>/ok[/<value>...]             e.g.  $17/ok
          $<tag>/err/<message>               e.g.  $17/err/system is disabled...
async   : $0/log/<text>                      informational; never a reply
```

Every request gets exactly one reply, echoing the host-chosen positive
integer `tag`. Booleans are `0`/`1`; floats accept any C-parseable form.
The full command list is the table at the bottom of `src/main.cpp`; argument
meanings are documented on the handlers and in the subsystem headers.

The protocol is debuggable by hand: connect a serial monitor and type
`$1/ping` or `$2/status`.

## Python client

Requires Python ≥ 3.11 and pyserial. Install with `pip install -e .` (or use
`uv run` inside the repo).

Minimal example — flash an LED and read a voltage:

```python
from syncboard import SyncBoard

with SyncBoard.connect() as sb:        # port auto-discovered by USB id
    sb.initialise(led_board=True)      # attach boards + enable the system

    sb.leds.calibrate(2)               # once per installed LED (stored in EEPROM)
    sb.leds.set_level(2, 0.10)         # 10% of the calibrated maximum
    sb.leds.pulse(2, duration_ms=50)

    volts = sb.io.read_adc(3)
# leaving the block disables the system and closes the port
```

More of the surface:

```python
from syncboard import SyncBoard, SignalMode, Frame

with SyncBoard.connect() as sb:
    sb.initialise(led_board=True, magnet_board=True)

    # Bench IO
    sb.io.write_do(1, True)
    sb.io.set_dac(2, 1.65)

    # Magnet (calibrate once per session, then set fields via Hall feedback)
    sb.magnet.calibrate()
    sb.magnet.calibrate_hall(0)
    sb.magnet.enable()
    sb.magnet.set_field(1.5)           # mT

    # Record 500 ADC samples at 2 ms spacing (one call, blocks until done)
    trace = sb.signals.record(SignalMode.ADC, channel=3, n_samples=500, interval_ms=2.0)

    # A repeating 100 Hz square wave on DAC 1
    sb.signals.configure(0, SignalMode.DAC, option=1, repeat=True)
    sb.signals.load(0, values=[3.0, 0.0], delays_ms=[5, 5])
    sb.signals.start(0)

    # A two-frame image sequence, host-triggered (sync mode 1)
    sb.imaging.set_sync_mode(1)
    sb.imaging.configure([Frame(led=2, exposure_ms=10), Frame(led=4, exposure_ms=10)])
    sb.imaging.start()
    sb.imaging.wait(timeout=2.0)   # blocks until the sequence completes
```

Errors reported by the firmware raise `syncboard.CommandError` with the
firmware's explanation. Asynchronous firmware output (warnings from the
signal engine, calibration notes) is emitted on the `"syncboard"` logger.

Leaving the `with` block disables the system (all outputs safe) and closes
the port; pass `sb.close(disable=False)` to skip that.

Run the unit tests with `uv run --group dev pytest` (no hardware needed).

## Hardware checkout

For bench verification after flashing or hardware changes, with a scope and
voltmeter at hand:

```sh
python hwtest/checkout.py                  # core sections (IO, DAC/ADC, signals...)
python hwtest/checkout.py --led 1          # also test LED channel 1
python hwtest/checkout.py --magnet         # also test the magnet board
python hwtest/checkout.py --sections do,dac
```

The script drives each subsystem, tells you what to probe and what to
expect, and prints a pass/fail summary.

## Migrating from v1 (pre-rewrite)

The 2024/25 firmware and client (`$cmd/arg#%` protocol, `SyncBoardController`
class) were rewritten together in 2026; see git history on `master` for the
old code. Not wire- or API-compatible: flash the firmware and update client
code in the same step. Notable behavioural changes:

- Every command now succeeds or fails explicitly; Python raises on failure.
- LED calibrations are stored in a new EEPROM layout — **recalibrate each
  LED once after first flashing** (`sb.leds.calibrate(...)`).
- The unimplemented filter-wheel/DMD/"other" flags of `setupImageSequence`
  and the GPIO PWM/analog modes were dropped.
- The magnet enable/select GPIO lines are (re)configured by `setupMagnet`
  after each system enable, not only at attach time.
- Wavelength→LED-channel mappings (e.g. 450 nm = channel 5) are application
  configuration and no longer live in this package. The per-channel rated
  current table survives as `LedApi.MAX_CURRENT_A` (the `calibrate` default).
