# LED board bring-up

Bring-up for the LED Control / Drive board. It's a high-power constant-current
LED driver (up to roughly 15-25 A per channel) controlled over I2C from the
SyncBoard. Schematics: `schematics/LEDControl1.SchDoc` (power, references,
per-channel config), `schematics/LEDDrive1.SchDoc` (the per-channel current
loop), `schematics/ODChannel.SchDoc` (scatter LED and photodiode).

This board sources very large currents, has a split ground, and several
jumpers with "don't connect both" rules. Work through the sections in order.
Sections 0 and 1 need no SyncBoard and are safe at a low current limit; the
LED drive rails physically cannot come up until a SyncBoard heartbeat is
present, so nothing can source current in those sections.

You need a current-limited 24 V bench supply, a multimeter, a scope, and for
sections 2 onwards a working SyncBoard and a power resistor.

## How the board works

Power comes in at 24 V on H2 and splits three ways:

- Vreg5, a small switcher, makes an intermediate rail called 6V5. That rail
  feeds only two LDOs: VReg3 (ADM7150-5.0) makes 5 V and VReg4 (ADM7150-3.3)
  makes 3V3. This is the logic and analog supply and it is always on.
- VReg_1 and VReg_2, two TDK i7A bricks, make the high-current LED drive
  rails VLedA and VLedB. VLedA feeds channels 1-5, VLedB feeds channels 7-8,
  and channel 6 picks one of them with a jumper. The output voltage is set by
  RTrim1 / RTrim2 (Vout = 16400/(Rtrim+825) + 3.28): with RTrim1 = 5.1k VLedA
  is about 6.0 V, and with RTrim2 = 2k VLedB is about 9 V. Each channel's LED
  gets its rail through a ferrite bead.
- The five fans sit directly on 24 V through J_FAN and return through GNDN.

The LED rail bricks are the -0F3 option, which is negative-logic remote
on/off: the brick is on only while its on/off pin is pulled to GNDN. A small
MOSFET does that pull-down, and its gate is the heartbeat HB (Sw-1 for VLedA;
Sw-2 for VLedB via jumper J1-5). So with HB low the LED rails are off and
read 0 V. That's the safe state, not a fault.

References: Ref1 (ADR4533) makes the precision 3V3Ref, which goes through
0 Ω links to the DAC's VRefIO and the ADC's REF pin. A 5k/500 divider off
3V3Ref, buffered by Opa_1, makes VL at 0.300 V. VL is the zero-current
reference for every channel's current-sense amp.

Each channel is a current loop: DAC setpoint into an integrator (Opa2), into
a gate-drive op-amp (Opa1, LM7321), into a MOSFET (M1, STP36NF06L, TO-220),
through the LED, through a 3 mΩ sense resistor, into an INA296A current-sense
amp at gain 50. That's 150 mV/A and zero current reads as VL. A D-latch
latches a channel off on over-current until its next turn-on.

Control lines from the SyncBoard, all through isolators whose input side is
powered by the SyncBoard's 3V3_IN / 5V_IN:

- HB, the heartbeat, on H1 pin 2 through Iso-3. On the SyncBoard a watchdog
  turns the Teensy's 5 Hz toggle into a steady level: 3V3 while the software
  is healthy, 0 V otherwise. On this board HB enables the LED rail bricks,
  gates every channel's Enable through an AND gate, and (inverted by N1 and
  routed through J1-HB) drives the PCA9685's output-enable so DA/DB drop to
  0 when it stops.
- EN1..8, the per-channel enables, come straight from Teensy pins 33-40
  (nets d16..d23) through Iso-2 / Iso-3. They do not go through the PCA9685.
- I2C through Iso-1. The PCA9685 (0x50) sets each channel's DA (current vs
  optical feedback) and DB (which signal goes back to the ADC).

The isolators are ISO7760F, the default-low variant, so with the SyncBoard
absent every one of these lines is driven to 0 V.

I2C chips, which show up in the SyncBoard scan once the board is attached:

| Chip | Address | Role |
|------|---------|------|
| AD5669 DAC | 0x57 | per-channel current/power setpoint |
| ADS7828 ADC | 0x49 | current / optical readback |
| PCA9685 PWM | 0x50 | per-channel DA / DB |

## 0. Before power

Everything here is with the supply off and the SyncBoard disconnected.

Jumpers:

- J_FAN out for the first power-up. It puts raw 24 V on all five fans, which on their own exceed a 0.2 A limit.
- J1-3A / J1-3B: at most one fitted. J1-3A gives channel 6 VLedA, J1-3B gives it VLedB. Never both ("MUST NOT CONNECT BOTH AT ONCE").
- J1-HB in 1-2. That sends the inverted heartbeat to the PCA9685 output
  enable, which is what forces DA/DB off when the software dies.
- J1-5 in 1-2 for HB to control VLedB, or 2-3 to keep VLedB permanently off.
- J1-4 picks channel 6's setpoint: 1-2 from the DAC (channel F), 2-3 from the SyncBoard's fast analog line through the isolated amplifier.
- J1-1 on each channel in 1-2 (the channel indicator follows Enable).

Meter checks:

- H2 pinout: pins 1 and 2 are GNDN, pins 3 and 4 are +24 V. Check against the housing before plugging in.
- 6V5, 5 V, and 3V3 to GND: no shorts.

## 1. Rails and references (24 V, no SyncBoard)

Set the supply to 24 V with the limit at 0.2 A. The current rises briefly
while about 3 mF of input capacitance charges, then settles. A good board
with no SyncBoard and J_FAN out idles at 43 mA at 24 V (measured on the
first build, 2026-09-09). If the next build settles somewhere else, that's
the first clue.

The indicator LEDs tell you most of it at a glance:

| LED | Net | Expect now |
|-----|-----|------------|
| LED1-2 | VIn | on |
| LED1-6 | 6V5 | on |
| LED1-5 | 5 V | on |
| LED1-1 | 3V3 | on |
| LED1-4 | VLedA | off |
| LED1-3 | VLedB | off |
| LED7 | HB | off |
| LED-8 | 5V_IN (SyncBoard side) | off |
| LEDC_1 on each of the 8 channels | channel Active | all off |

Then probe, black lead on a T-point ground clip:

| Where | Expect |
|-------|--------|
| Ferrite F1, either end (6V5) | Vreg5 output, about 6.5 V |
| VReg3 output (5 V) | 5.0 V |
| VReg4 output (3V3) | 3.3 V |
| Ref1 pin 6 (3V3Ref) | 3.300 V |
| Opa_1 output / R9-1 (VL) | 0.300 V = 3V3Ref × 500/5500 |
| DAC_1 pin 8 (VRefIO) and ADC1 pin 10 (REF) | same as 3V3Ref |
| VLedA at CE-2, VLedB at CE-4 | 0 V (bricks off, HB low) |

Get 3V3Ref and VL right before anything else. Ref1 is the same ADR family
that arrived dead at 1.0 V on a SyncBoard, and a good reading on the 3V3 rail
tells you nothing about it since it's a different net. If VL is wrong but
3V3Ref is fine, look at R7-1 (500), R8-1 (5k), and the OPA388 buffer. If VL is
off, every current reading and zero point on all eight channels is off.

After a few minutes feel Vreg5, the two LDOs, and the per-channel op-amps.
One hot chip means a fault. That's the end of the standalone checks.

## 2. Heartbeat, rails, and I2C (needs a SyncBoard)

The heartbeat can't be driven without a SyncBoard, so the safe-state check
and its release are done together here: first with the SyncBoard attached
but not enabled (HB low, everything forced off), then after `enable` (HB
high, LED rails up). You get to watch the transition, which is the real
test of the interlock.

Before connecting:

- Raise the supply limit to 1-2 A. When HB goes high both LED rail bricks
  start and charge their output capacitors, and at 0.2 A they'll brown out.
- Fit J_FAN and confirm all five fans spin (four for the LED drivers, a
  fifth for the power converters). Note the extra current. Sustained LED
  current needs the fans running before you turn it on.

`hwtest/led_board.py` walks the rest of this section and the next one,
driving the SyncBoard and asking you to confirm each reading:

```sh
uv run python hwtest/led_board.py                      # link, heartbeat, i2c, enables
uv run python hwtest/led_board.py --sections heartbeat # one section at a time
```

The steps it takes, in order:

1. Attach, don't enable. Connect the cable and power both boards. LED-8
   (5V_IN, LINK) lights. HB is still low, so check the forced-off state:

   | Where | Expect |
   |-------|--------|
   | HB (N1 pin 2, or LED7's series resistor) | 0 V |
   | PWM1 pin 23 (output enable, inverted HB) | 3.3 V |
   | VLedA at CE-2, VLedB at CE-4 | 0 V |
   | M1 pin 1 (gate) on each of the 8 channels | 0 V |
   | LED1-4, LED1-3, LED7, all eight LEDC_1 | off |

   The forcing path per channel is: HB low, so the AND gate output Enable
   is low, the D-latch output is low, Active is low, and analog switch Sw1
   puts the gate driver input at ground.

2. Enable. Run `attachLed` then `enable`. LED7 lights, HB reads about
   3.3 V, PWM1 pin 23 drops to 0 V, and LED1-4 / LED1-3 light as VLedA and
   VLedB come up. Measure both rails and compare with the values you
   expected from the trim resistors. The channel gates and all eight LEDC_1
   stay at 0 V / off because no EN is asserted yet.

3. Kill the heartbeat. Run `disable` (or unplug the cable) and confirm
   everything in step 1 comes back: HB 0 V, both rails collapse, LED7 off.
   This is the interlock doing its job. Re-enable afterwards.

4. I2C scan. Run `scanI2c` and confirm 0x49, 0x50, and 0x57 all answer.

## 3. Per-channel function (needs a SyncBoard, and care)

5. Enables. If only some channels are populated, pass them with
   `--channels 5` (or `1,2,5`) so the script leaves the rest alone. The
   `enables` section pulses each listed channel for a second
   with its DAC at 0 V, so the LEDC_1 indicators walk 1 to 8 with no
   current flowing. That covers the Teensy pins, isolators, AND gates and
   D-latches. It then reads the ADC back on every channel, which proves the
   DB mux on the PCA9685 and the ADS7828 answer.
6. Current loop, one channel at a time, into a power resistor rated for the
   current, not a real LED:

   ```sh
   uv run python hwtest/led_board.py --sections current --channel 1 --max-current 2
   ```

   This calibrates the channel (a sweep up to the max you give, so start
   low), holds it at 20% level, and compares the board's own current
   reading with your meter. At that point the sense pin should read
   VL + 0.15 V/A × I.
7. Optical path and ODChannel. The main optical-feedback path and the
   separate scatter LED (about 50 mA) with its transimpedance photodiode
   (LTC6268) come later, with light applied.

## Hazards to keep in mind

- 0.2 A at 24 V is idle only. LED current comes from the bricks and needs
  real headroom; trying to fire an LED at that limit just browns them out.
- Use a power resistor before any real LED, and start at a low max current.
- Fans on before sustained current.
- The split ground (GNDN carries the noisy LED return current and ties to
  the rest through JG near the power input) is there for a reason; don't
  defeat it.
- The MOSFET was swapped to the STP36NF06L, which has higher input
  capacitance, and the designer notes its switching speed is unproven. Scope
  the gate and channel current during timed or fast switching once you're
  driving a load.

## Known-good values

| Quantity | Expected |
|----------|----------|
| Logic rails | 6V5 (LDO input), 5 V, 3V3 |
| LED rails (only with HB high) | VLedA ≈ 6.0 V (RTrim1 5.1k), VLedB ≈ 9 V (RTrim2 2k) |
| VL | 0.300 V |
| 3V3Ref | 3.300 V |
| Current sense | VL + 150 mV/A (3 mΩ × 50) |
| Idle @ 24 V, no SyncBoard, J_FAN out | 43 mA |
| I2C scan (attached) | 0x49, 0x50, 0x57 |
| Safe state (no HB) | VLedA/B 0 V, all gates 0 V, PWM OE high |
