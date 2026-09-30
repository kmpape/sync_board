# SyncBoard bring-up

How to bring up a freshly soldered SyncBoard. Work through it in order and
run one `hwtest/checkout.py` section at a time so that when something fails
you know which layer it was.

You need a current-limited 12 V bench supply, a multimeter, a scope, and a
few jumper wires.

## 0. Before power

- Check continuity from GND to the 3V3 / 5V / 12V rails. Solder bridges on
  the fine-pitch I2C chips are the usual problem.
- Look at the SDA/SCL pins. The whole board is on that one bus, so a bridge
  there takes everything down.

## 1. Rails

Power the 12 V input with the supply limit at about 0.5 A. Check 12 V, 5 V,
and 3V3.

A good board with the Teensy in and running off this rail draws about
125 mA at 12 V. If you set the limit to 0.1 A it trips on the Teensy alone
and the supply drops to around 5 V. Note the actual idle current; it's the
quickest health check on the next build.

Where to probe (black on GND, either a Teensy GND pin or the input
connector's negative):

| Rail | Probe point |
|------|-------------|
| 12 V | positive pin of the power input connector |
| 5 V  | Teensy VIN pin (top-right corner, by USB) = board 5 V regulator output |
| 3V3 (Teensy) | Teensy 3.3V pin (right side, two down from VIN) |
| 3V3 (board) | SDA or SCL (pins 18/19); the bus pull-ups hold it at 3V3 when idle |

### Powering the Teensy from the board (R4-3 and the data-only cable)

The board can power the Teensy through its Vin pin, but that's not how it
ships:

- R4-3 is a do-not-populate jumper between the board 5 V net and the Teensy
  Vin pin. With it empty (the default), board 5 V never reaches the Teensy,
  so the Teensy runs off USB and goes dead when USB is unplugged. To run the
  Teensy off the board, bridge R4-3 with a 0 Ω link or a solder blob. Check
  it first with the meter (power off): one R4-3 pad to the 5 V net should be
  0 Ω, the other to Teensy Vin should be 0 Ω.
- Once R4-3 is bridged, board 5 V also reaches USB VBUS through the Teensy's
  underside VUSB-VIN pad, where it fights the computer's 5 V. The clean fix
  is to cut that pad, but it's under the Teensy and you can't reach it once
  it's soldered down. Instead use a data-only USB cable: a normal cable with
  the red (VBUS) wire cut out, data and GND left alone. Label it. With R4-3
  bridged and a data-only cable, the board powers the Teensy and USB carries
  only data. That's the wiring you want for the finished instrument.
- To check the cable: with board power off it should leave the Teensy dead
  (nothing enumerates); with board power on the Teensy boots and `$1/ping`
  answers.

If R4-3 is still empty, the Teensy runs off USB and there's no conflict
(board 5 V and USB VBUS are separated by the empty R4-3), so an ordinary
cable is fine for bench work.

## 2. Smoke test

Connect USB, flash, and run the first section:

```sh
pio run -t upload
python hwtest/checkout.py --sections system
```

The I2C scan is the gate. It has to find 0x40 (switch PWM), 0x48 (ADC), and
0x60 (level-shift PWM). An empty scan means SDA/SCL; a missing address means
that one chip. Don't go on until the scan is clean.

While the system is enabled, watch the display counter wheel. It runs off
the heartbeat, so a turning wheel with all its LEDs lit is the visual
heartbeat check. You can also scope the heartbeat on pin 41 (about 5 Hz).

## 3. Core sections, one at a time

Run each with `python hwtest/checkout.py --sections <name>`:

- `do` — digital outputs 1-4, buffered to 5 V on the connector side.
  Voltmeter on each D_OUT, scope for the pulse train (it runs until you
  answer, so there's no rush to arm the scope).
- `di` — digital inputs 1-4, 5 V logic. Jumper D_OUT_1 to each D_IN when
  asked; the script toggles D_OUT_1 and checks both states. Run `do` first
  since it's the source.
- `dac` — SPI plus the AD5668. Voltmeter on each channel's DAC_n SMA. The
  script asks you to set the DAC SMA routing jumpers S1-16..S1-19 to the S
  position (pins 1-2) first. Channel 1 gets a three-point sweep (which also
  checks the reference); 2-8 get one mid-scale reading.
- `adc` — the ADS7828. Each channel is checked by looping DAC_n into ADC_n
  with an SMA cable. Run `dac` first: the two share the Ref1 reference, so a
  bad reference cancels out in the loopback and you'd miss it.
- `gpio` — all nine GPIOs. The seven level-shifted ones (13, 25-28, 31, 32)
  loop back into D_IN_1 one at a time (jumper when asked, level-select
  jumpers on 5 V), plus an input-direction check on GPIO25. GPIO29/30 have
  no shifter and get a 3.3 V voltmeter check. GPIO29-32 come out on the
  Ain0/Ain1 and PWM0/PWM1 connectors depending on jumpers J1-20..J1-23 (the
  other position sends them to the expansion headers); set these before the
  section. Don't drive 5 V into GPIO29/30 directly — they're unshifted and
  the Teensy isn't 5 V tolerant.
- `switches` — the 12 V switch path. These are low-side drivers: channel n
  pulls net Po(n-1) on the power-switch headers to GND when on, and loads go
  between +12 V and the Po pin. Voltmeter on the Po pins; about 0 V is on.
  Off floats high on a small diagnostic current, so with nothing connected
  "off" reads around 11.4 V rather than a clean 12 V. That's fine.
- `signals` — the timing engine. Scope a 100 Hz square wave on DAC 1 and a
  20 Hz conductor/slave train on D_OUT_1. It also runs an ADC recording
  check and a 2000-step (~50 kB) upload/readback to stress the protocol over
  USB.

## 4. Camera interface

```sh
python hwtest/checkout.py --sections imaging
```

Checks the six camera input lines (driven from D_OUT_1 with a jumper and
read back over the protocol) and, with a scope on the trigger line, the
camera trigger output pulse. Run it again with the real camera wired when
you integrate. `sb.imaging.read_camera_inputs()` is also how you check
whether the camera is asserting ready.

## 5. Expansion boards

Attaching a board adds its chips to the I2C scan: the LED board adds
0x49/0x50/0x57, the magnet board 0x4A/0x54. See [led-board.md](led-board.md)
for the LED board. Calibrate a LED channel into a power resistor first, not
a real LED — calibration sweeps up to the current limit you give it. Magnet
calibration drives real coil current.

## Known-good values

| Quantity | Expected |
|----------|----------|
| Idle current | ~125 mA @ 12 V (Teensy on board power, no USB load) |
| I2C scan (bare board) | 0x40, 0x48, 0x60 |
| Heartbeat | ~5 Hz square wave, pin 41 |
| D_OUT high | ~5 V (buffered) |
| GPIO29/30 high | ~3.3 V (unshifted) |
| DAC full scale | 3.3 V (= Ref1); sweep lands on 0.5 / 1.65 / 3.0 V |
