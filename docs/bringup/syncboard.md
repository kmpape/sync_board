# SyncBoard bring-up

Bring-up procedure for a freshly soldered SyncBoard (Teensy 4.1 main board).
Work up to the full `hwtest/checkout.py` run in stages; each stage adds one
hardware layer so a failure is attributable.

You need: a current-limited bench supply (12 V), a multimeter, an
oscilloscope, and a few jumper wires.

## 0. Before power

- Continuity-check GND against the 3V3 / 5V / 12V rails (solder bridges on
  the fine-pitch I2C chips are the classic fault).
- Inspect the SDA/SCL pins — the whole board hangs off that one bus.

## 1. Rails

Power the 12 V input from the bench supply with the current limit set to
**~0.5 A**, and verify 12 V / 5 V / 3V3.

- With the Teensy inserted and powered from this rail, expect **~125 mA @
  12 V idle** (measured on a known-good board). A 0.1 A limit trips on the
  Teensy alone and the supply collapses into constant-current mode (≈5 V).
- Note your board's actual idle current — deviation from it is the fastest
  health check.

Where to probe (black on GND — a Teensy GND pin, or the input connector's
negative):

| Rail | Probe point |
|------|-------------|
| 12 V | positive pin of the power input connector |
| 5 V  | Teensy **VIN** pin (top-right corner, by USB) = board 5 V regulator output |
| 3V3 (Teensy) | Teensy **3.3V** pin (right side, two down from VIN) |
| 3V3 (board) | SDA or SCL line (pins 18/19); the bus pull-ups hold it at the 3V3 rail when idle |

### Powering the Teensy from the board (R4-3 + data-only USB)

The board can power the Teensy through its Vin pin, but this is **not the
default assembly state**:

- **`R4-3` is a do-not-populate jumper** between the board 5 V net and the
  Teensy Vin pin. Un-populated (the shipped state), board 5 V does **not**
  reach the Teensy — it runs on USB power only, and goes dead when USB is
  unplugged. To board-power the Teensy (so it runs whenever the instrument
  is powered), **bridge R4-3** with a 0 Ω link / solder blob. Confirm first
  with the meter (power off): one R4-3 pad to the 5 V net ≈ 0 Ω, the other
  to Teensy Vin ≈ 0 Ω.
- **Once R4-3 is populated**, board 5 V would also reach USB VBUS through
  the Teensy's underside VUSB↔VIN pad and fight the computer's 5 V. Prevent
  this by either cutting that pad (inaccessible once the Teensy is soldered
  down) or — the practical fix — using a **data-only USB cable**: a normal
  cable with the VBUS (red) conductor cut out, data and GND intact, clearly
  labelled. With R4-3 populated + a data-only cable, the board powers the
  Teensy and USB carries only data. This is the intended deployment wiring.
- Verify: with a data-only cable and board power off, the Teensy stays dead
  (no enumeration); with board power on, it boots and `$1/ping` answers.

Until R4-3 is populated, the Teensy is USB-powered; there is no supply
conflict (board 5 V and USB VBUS are isolated by the empty R4-3), so a
normal cable is fine for bench work.

## 2. Smoke test

Connect USB, flash, and run the first checkout section:

```sh
pio run -t upload
python hwtest/checkout.py --sections system
```

The **I2C scan is the key gate**: it must find **0x40** (switch PWM),
**0x48** (ADC), and **0x60** (level-shift PWM). An empty scan means SDA/SCL;
one missing address means that chip. Do not continue past a failing scan.

While the system is enabled, also watch the **display counter wheel**: it is
driven by the heartbeat, so a turning wheel with all its LEDs lighting is the
visual heartbeat check. (You can also scope the heartbeat on pin 41: ~5 Hz
square wave.)

## 3. Core sections, one at a time

Run each with `python hwtest/checkout.py --sections <name>`:

- **`do`** — digital outputs 1–4 (buffered to **5 V** on the connector
  side); voltmeter on each D_OUT, scope for the pulse train (runs until you
  answer, so no rush to arm the scope).
- **`di`** — digital inputs 1–4 (5 V logic); jumper D_OUT_1 to each D_IN when
  asked — the script toggles D_OUT_1 and verifies both states itself. Run
  `do` first (it is the stimulus source).
- **`dac`** — SPI path + AD5668; voltmeter on each channel's **DAC_n SMA**
  connector. The script first asks to set the DAC SMA routing jumpers
  **S1-16..S1-19** to the S position (pins 1-2). Channel 1 gets a 3-point
  sweep (validates the reference too); 2–8 get a mid-scale spot check.
- **`adc`** — ADS7828; each channel is auto-verified by looping **DAC_n into
  ADC_n** with an SMA cable. **Requires `dac` to have passed first** — the
  two share the Ref1 reference, so a bad reference cancels out in loopback
  and would go undetected.
- **`gpio`** — all nine GPIOs. The seven level-shifted ones (13, 25–28, 31,
  32) loop back into D_IN_1 one at a time (jumper when asked, level-select
  jumpers set to 5 V), plus an input-direction check on GPIO25. GPIO29/30
  have no shifter and get a 3.3 V voltmeter check. GPIO29–32 surface on the
  **Ain0/Ain1** and **PWM0/PWM1** connectors via jumpers **J1-20..J1-23**
  (other position: expansion headers) — set these before the section.
  **Never drive 5 V into GPIO29/30 directly** — they are unshifted and the
  Teensy is not 5 V tolerant.
- **`switches`** — the 12 V switch path. These are **low-side** drivers:
  channel n sinks net **Po(n−1)** (on the power-switch headers) to GND when
  on; loads connect between +12 V and the Po pin. Voltmeter on the Po pins;
  ~0 V means on, drifting-high means off (a diagnostic trickle, so an
  unloaded "off" reads ~11.4 V, not a clean 12 V — that is normal).
- **`signals`** — the timing engine; scope a 100 Hz square wave on DAC 1 and
  a 20 Hz conductor/slave train on D_OUT_1, an automatic ADC recording
  check, and a 2000-step (~50 kB) upload/readback that stresses the protocol
  on the real link.

## 4. Camera interface

```sh
python hwtest/checkout.py --sections imaging
```

Verifies the six camera **input** lines (driven from D_OUT_1 via jumper and
read back over the protocol) and, with a scope on the trigger line, the
camera trigger **output** pulse. Repeat with the real camera wired when
integrating — `sb.imaging.read_camera_inputs()` is also the debugger for
"is the camera actually asserting ready?".

## 5. Expansion boards

Attaching a board adds its chips to the I2C scan: the LED board adds
0x49/0x50/0x57, the magnet board 0x4A/0x54. See [led-board.md](led-board.md)
for LED-board bring-up. Calibrate a LED channel first with a **dummy load**
(a power resistor), not an expensive LED — calibration sweeps to the current
limit you give it. Magnet calibration drives real coil current.

## Known-good reference values

| Quantity | Expected |
|----------|----------|
| Idle current | ~125 mA @ 12 V (Teensy board-powered, no USB load) |
| I2C scan (bare board) | 0x40, 0x48, 0x60 |
| Heartbeat | ~5 Hz square wave, pin 41 |
| D_OUT high | ~5 V (buffered) |
| GPIO29/30 high | ~3.3 V (unshifted) |
| DAC full-scale | 3.3 V (= Ref1); sweep lands on 0.5 / 1.65 / 3.0 V |
