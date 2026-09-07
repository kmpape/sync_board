# LED board bring-up

Bring-up for the LED Control / Drive board. It's a high-power constant-current
LED driver (up to roughly 15-25 A per channel) controlled over I2C from the
SyncBoard. Schematics: `schematics/LEDControl1.SchDoc` (power, references,
per-channel config), `schematics/LEDDrive1.SchDoc` (the per-channel current
loop), `schematics/ODChannel.SchDoc` (scatter LED and photodiode).

This board sources very large currents, has a split ground, and several
jumpers with "don't connect both" rules. Do all the passive and safety
checks before letting any channel source current, and use a power resistor
in place of a real LED the first time.

You need a current-limited 24 V bench supply, a multimeter, a scope, and for
functional testing a working SyncBoard.

## How the board works

24 V comes in, a switching regulator makes the high-current LED drive rail(s)
(VLedA / VLedB, about 6.5 V as built, set by Rtrim), and those feed each
channel (VLed1..8) through per-LED ferrite beads. Low-noise LDOs make the
5 V and 3V3 logic rails. There's a precision 3V3 reference (3V3Ref) and a
buffered low reference VL at about 0.3 V for the analog side.

Each channel is a current loop: DAC setpoint into an integrator/comparator
op-amp, into a gate-drive op-amp, into a MOSFET (STP36NF06L), through the
LED, through a 3 mΩ sense resistor, into an INA296A current-sense amp at
gain 50. That's 150 mV/A, and zero current reads as VL. A D-latch latches a
channel off on over-current until its next turn-on. A PCA9685 (0x50) sets
each channel's DA (current vs optical feedback), DB (ADC return select), and
EN (enable).

The heartbeat is the watchdog. HB is 3V3 when the software is healthy and 0 V
otherwise, and when HB_bar goes high every output is forced to 0 V.

I2C chips, which show up in the SyncBoard scan once the board is attached:

| Chip | Address | Role |
|------|---------|------|
| AD5669 DAC | 0x57 | per-channel current/power setpoint |
| ADS7828 ADC | 0x49 | current / optical readback |
| PCA9685 PWM | 0x50 | per-channel DA / DB / EN |

## 1. Rails and references (24 V, standalone)

Idle draw is small, so 0.2 A at 24 V is enough to power up the logic and
analog. It is not enough to fire an LED (see the hazards at the bottom).
Beyond the obvious 6V5 / 5 V / 3V3, check:

1. VL, about 0.3 V. This is the zero-current reference for every channel's
   current-sense amp (zero current reads as VL). If VL is off, every current
   reading and zero point is off on all eight channels, so it's worth
   getting right first.
2. 3V3Ref, 3.3 V. This is the precision reference, an ADR-family part, the
   same as the SyncBoard's Ref1. Check it on its own pin. Ref1 on at least
   one SyncBoard was dead on arrival at 1.0 V, and a good reading on the
   "3V3" rail doesn't tell you anything about it since it's a different net.
3. VLedA / VLedB, the high-current drive rail(s). Confirm the voltage suits
   the LEDs you'll run (it's Rtrim-dependent, about 6.5 V as built). Channel
   6 has a jumper picking VLedA or VLedB, and only one may be connected at a
   time ("MUST NOT CONNECT BOTH AT ONCE").

## 2. Safety interlock and cooling (standalone)

4. Heartbeat safe state. With no SyncBoard connected, HB sits at 0 V, HB_bar
   is high, and all outputs are forced to 0 V. Confirm the board really is
   dead: the eight channel enable / MOSFET-gate nodes at 0 V, no channel
   able to source current. Checking the watchdog works now, before any LED
   is connected, is the right time to find out it doesn't.
5. Fans and heat. Note the idle current and feel for heat; one hot chip
   means a fault. Find the fan jumpers (four fans for the LED drivers, a
   fifth for the power converters) and check the fans spin. Sustained LED
   current needs the fans running before you turn it on.

## 3. I2C and heartbeat release (needs a SyncBoard)

6. I2C scan. Connect to a working SyncBoard, run `attachLed`, `enable`,
   `scanI2c`, and confirm 0x49, 0x50, and 0x57 all answer. A SyncBoard's own
   Ref1 fault doesn't matter here since this board has its own reference and
   chips. The SyncBoard does need R4-3 bridged and a data-only cable so its
   Teensy actually runs (see [syncboard.md](syncboard.md)).
7. Heartbeat release. With the SyncBoard enabled and its heartbeat running,
   check HB on the LED board goes to about 3.3 V and the forced-off state
   releases.

## 4. Per-channel function (needs a SyncBoard, and care)

8. Per-channel mux. Through the PWM (0x50), check DA (current/optical
   feedback), DB (ADC return select), and EN switch when commanded.
9. Calibrate into a power resistor rated for the current, not a real LED.
   Calibration sweeps the current up toward the limit and this board can
   push 15-25 A. Start with a low max current.
10. Current sense. At a known setpoint the sense output should read
    VL + 0.15 V/A × I. Cross-check against the voltage across the dummy
    resistor or an inline ammeter.
11. Optical path and ODChannel. The main optical-feedback path and the
    separate scatter LED (about 50 mA) with its transimpedance photodiode
    (LTC6268) come later, with light applied.

## Hazards to keep in mind

- 0.2 A at 24 V is idle only. LED current comes from the switcher and needs
  real headroom; trying to fire an LED at that limit just browns out the
  switcher.
- Use a power resistor before any real LED, and start at a low max current.
- Fans on before sustained current.
- The split ground (GNDN carries the noisy LED return current and ties to
  the rest near the power input) is there for a reason; don't defeat it.
- The MOSFET was swapped to the STP36NF06L, which has higher input
  capacitance, and the designer notes its switching speed is unproven. Scope
  the gate and channel current during timed or fast switching once you're
  driving a load.

## Known-good values

| Quantity | Expected |
|----------|----------|
| Rails | 6V5 (VLed), 5 V, 3V3 |
| VL | ~0.3 V |
| 3V3Ref | 3.3 V |
| Current sense | VL + 150 mV/A (3 mΩ × 50) |
| Idle @ 24 V | small (fits in a 0.2 A limit) |
| I2C scan (attached) | 0x49, 0x50, 0x57 |
| Safe state (no HB) | all channel outputs 0 V |
