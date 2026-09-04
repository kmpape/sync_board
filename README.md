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
schematics/     board schematic (SyncBoard1.SchDoc, Altium)
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
Argument semantics are documented on the handlers in `src/main.cpp` and in
the subsystem headers.

The protocol is debuggable by hand: connect a serial monitor and type
`$1/ping` or `$2/status`.

### Command reference

Unless noted, commands reply with a bare `ok`. Most require an enabled
system; `attachLed`, `attachMagnet` and `setupGpio` require a *disabled*
one. Channels: DO/DI 1–4, ADC/DAC/LED 1–8 (DAC 0 = all), GPIO by label
(13, 25–32), switches 1–16 (0 = all off), Hall sensors 0–2.

| Command | Arguments | Ok reply values |
|---|---|---|
| `ping` | — | `syncboard`, version, uptime ms |
| `status` | — | enabled, ledAttached, magnetAttached, syncMode |
| `activity` | — | active-signal bitmask, imaging running |
| `enable` / `disable` | — | — (disable blocks ~1 s) |
| `resetConfig` | — | — |
| `factoryReset` | — | — (erases EEPROM/calibrations) |
| `attachLed` / `attachMagnet` | present | — |
| `scanI2c` | — | count, addresses… |
| `readDi` | channel | 0/1 |
| `writeDo` | channel, high | — |
| `pulseDo` | channel, ms | — (≤ 60 s) |
| `setupGpio` | label, enabled, isInput | — (applied at next enable) |
| `writeGpio` / `readGpio` | label [, high] | — / 0/1 |
| `readAdc` | channel [, adcId 0=sync 1=LED 2=magnet] | volts |
| `setDac` | channel, volts | — (0–3.3 V) |
| `setSwitch` | channel, duty 0–1 | — |
| `calibrateLed` | channel, maxCurrentA | — (blocks seconds; stores to EEPROM) |
| `setLedLevel` | channel, level 0–1, feedback 0=current 1=optical | — |
| `switchLed` | channel, on | — (refused untimed > 30 % level) |
| `switchLedTimed` | channel, ms | — (≤ 30 min) |
| `disableLeds` | — | — |
| `measureLed` | channel | current A, optical mV |
| `measurePhotodiode` | channel | mV |
| `getLedSetup` | channel | level, current A, optical mV, max A |
| `setupMagnet` | — | — (re-run after every enable) |
| `enableMagnet` | on | — |
| `selectMagnetOutput` | nc | — |
| `calibrateMagnet` | — | — (blocks seconds; drives the coil) |
| `calibrateHall` | hallId | — |
| `setMagnetCurrent` | nc, value | — (calibrated units) |
| `setMagnetField` | nc, mT | — (needs Hall calibration) |
| `readHall` | hallId | mT |
| `readMagnetAdc` | channel | volts |
| `setMagnetDac` | channel, volts | — (raw; bring-up use) |
| `setupSignal` | index, mode, option, repeat, isSlave | — (invalidates loaded data) |
| `loadSignal` | index, n, value₀, delayMs₀, … | — (negative delay ends sequence) |
| `loadSignalUniform` | index, n, intervalMs | — |
| `startSignal` / `stopSignal` | index | — |
| `readSignal` | index | n, values… |
| `setSyncMode` | mode 0–2, ledByCamera | — |
| `setupImaging` | 4 × (active, led, exposureMs) | — |
| `startImaging` | numFrames | — (sanity-checked against config) |

Signal `mode` values match `syncboard.SignalMode` (documented in
`src/Signals.h`); `option` is the target channel/pin/sensor, or the slave
bitmask for a conductor.

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

**Enable logging to see firmware messages.** Without logging configured,
Python prints firmware WARNINGs/ERRORs bare to stderr and silently drops
INFO-level output — which is where the useful detail lives (calibration
slopes, sequence timings, the boot banner). Put this at the top of your
script:

```python
import logging
logging.basicConfig(level=logging.INFO)
```

or, to see only the SyncBoard's messages without turning on INFO globally:

```python
log = logging.getLogger("syncboard")
log.setLevel(logging.INFO)
log.addHandler(logging.StreamHandler())
```


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

### Bringing up a newly built board

For a freshly soldered board, work up to the full checkout in stages:

1. **Before power**: continuity-check GND against the 3V3/5V/12V rails
   (solder bridges on the fine-pitch I2C chips are the classic fault), and inspect the SDA/SCL pins.
2. **Rails**: power the 12 V input from a current-limited bench supply
   with the current limit set to ~0.5A. and verify 12 V/5 V/3V3. With the Teensy inserted, expect **~125 mA @ 12 V idle** (measured on a known-good board). Note your board's actual idle current: deviation from it is the fastest health check.
   **Before combining board power with USB**: cut the VUSB↔VIN pad on the
   Teensy's underside (see the note on the schematic). Uncut, USB ties the
   computer's 5 V to the board's 5 V regulator output and the two supplies
   fight. If the Teensy is already soldered down (pad inaccessible), use a
   **data-only USB cable** instead — a cable with the VBUS (red) conductor
   cut, data and GND intact; label it. Until one of the two is done, use
   board power *or* USB, never both.
3. **Smoke test**: connect the Teensy USB to a computer, run `pio run -t upload`, then `python hwtest/checkout.py --sections system`. The I2C scan is the key gate: it must find 0x40 (switch PWM), 0x48 (ADC) and 0x60 (level-shift PWM). An empty scan means SDA/SCL; one missing address means that chip. Do not continue past a failing scan. While the system is enabled, also watch the display counter wheel: it is driven by the heartbeat, so a turning wheel with all of its LEDs lighting is the visual heartbeat check.
4. **Core sections, one at a time** (`python hwtest/checkout.py --sections <name>`),
   each adding one hardware layer:
   - `do` — digital outputs 1–4 (buffered to **5 V** on the connector side);
     voltmeter on each D_OUT, scope for the pulse train.
   - `di` — digital inputs 1–4 (5 V logic); jumper D_OUT_1 to each D_IN when
     asked — the script toggles D_OUT_1 and verifies both states itself.
   - `dac` — SPI path + AD5668; voltmeter on each channel's DAC_n SMA
     connector. The script first asks to set the DAC SMA routing jumpers
     S1-16..S1-19 to the S position (pins 1-2).
   - `adc` — ADS7828; apply a known voltage (e.g. the 3V3 rail) to the
     ADC_1 SMA, then GND (channels 1–4 need their S/E routing jumper in S).
   - `gpio` — all nine GPIOs. The seven level-shifted ones (13, 25–28,
     31, 32) loop back into D_IN_1 one at a time (jumper when asked,
     level-select jumpers set to 5 V); GPIO29/30 have no shifter and get a
     3.3 V voltmeter check. GPIO29–32 surface on the Ain0/Ain1 and
     PWM0/PWM1 connectors via jumpers J1-20..J1-23 (other position:
     expansion headers) — the script walks through this.
   - `switches` — 12 V switch path; voltmeter on switch outputs 1 and 16.
   - `signals` — the timing engine; scope a 100 Hz square wave on DAC 1 and
     a 20 Hz conductor/slave train on D_OUT_1, plus an automatic ADC
     recording check.
5. **Expansion boards**: the LED board adds I2C 0x49/0x50/0x57, the magnet
   board 0x4A/0x54. Calibrate a LED channel first with a dummy load (a
   power resistor), not an expensive LED — calibration sweeps to the
   current limit you give it. Magnet calibration drives real coil current.
6. Finally, with the camera wired: `--sections imaging` with a scope on the
   trigger line.

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
