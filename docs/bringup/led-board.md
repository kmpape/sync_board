# LED board bring-up

Bring-up for the LED Control / Drive board — a high-power (up to ~15–25 A per
channel) precision constant-current LED driver, controlled over I2C from the
SyncBoard. Schematics: `schematics/LEDControl1.SchDoc` (power, references,
per-channel config), `schematics/LEDDrive1.SchDoc` (per-channel current
feedback loop), `schematics/ODChannel.SchDoc` (scatter LED + photodiode).

**Respect this board.** It sources very large currents from a switching
regulator, has a split ground, and several jumpers with "must not connect
both" rules. Work through the passive and safety checks fully before any
channel is allowed to source current, and use a dummy load before any real
LED.

You need: a 24 V bench supply (current-limited), a multimeter, an
oscilloscope, and — for functional testing — a working SyncBoard.

## Architecture in one paragraph

24 V in → switching regulator → the high-current LED drive rail(s)
(**VLedA / VLedB**, ~6.5 V as built, Rtrim-set) distributed per channel
(VLed1..8) through per-LED ferrite beads. Low-noise LDOs derive **5 V** and
**3V3** logic rails; a precision **3V3Ref** and a buffered low reference
**VL (~0.3 V)** feed the analog. Each channel is a precision current loop:
DAC setpoint → integrator/comparator op-amp → gate-drive op-amp → MOSFET
(**STP36NF06L**) → LED → 3 mΩ sense resistor → INA296A current-sense amp
(gain 50, so **150 mV/A**, zero current = VL). A D-latch latches an
over-current channel off until its next turn-on. A PCA9685 (**0x50**) sets
per-channel **DA** (current vs optical feedback), **DB** (ADC return select),
and **EN** (enable). The **heartbeat (HB)** is the watchdog: HB = 3V3 when
software is healthy, 0 V otherwise, and **HB_bar high forces every output to
0 V**.

I2C chips (added to the SyncBoard scan when attached):

| Chip | Address | Role |
|------|---------|------|
| AD5669 DAC | 0x57 | per-channel current/power setpoint |
| ADS7828 ADC | 0x49 | current / optical readback |
| PCA9685 PWM | 0x50 | per-channel DA / DB / EN config |

## 1. Rails and references (standalone, 24 V)

Idle draw is small; a **0.2 A limit at 24 V** is enough for the logic/analog
to power up (it is *not* enough to fire an LED — see safety). Verify, beyond
the obvious 6V5 / 5 V / 3V3:

1. **VL ≈ 0.30 V** — the single most important node. It is the zero-current
   reference for every channel's current-sense amp ("zero current = VL"). If
   VL is wrong, every current reading and zero-point is wrong on all 8
   channels.
2. **3V3Ref = 3.3 V** — the precision reference (ADR-family, same part as the
   SyncBoard's Ref1). **Check it explicitly** — Ref1 on at least one
   SyncBoard was dead-on-arrival at 1.0 V. "3V3" reading fine does not cover
   this; the reference is a separate net.
3. **VLedA / VLedB** — the high-current drive rail(s). Confirm the value
   matches the LEDs you intend to run (Rtrim-dependent; ~6.5 V as built).
   Channel 6 has a jumper selecting VLedA *or* VLedB — **must be on exactly
   one** ("MUST NOT CONNECT BOTH AT ONCE").

## 2. Safety interlock and thermals (standalone)

4. **Heartbeat safe-state.** With no SyncBoard, HB = 0 V → HB_bar high → all
   outputs forced to 0 V. Verify the board is genuinely **inert**: the 8
   channel enable / MOSFET-gate nodes at 0 V, no channel able to source
   current. Confirming the watchdog works now — before any LED is connected
   — is exactly when you want to know it does.
5. **Thermals / fans.** Note the idle current and feel for heat (a single hot
   chip = fault). Find the fan jumpers (4 fans for the LED drivers, a 5th for
   the power converters) and confirm the fans spin — sustained LED current
   needs cooling *before* it is turned on.

## 3. I2C and heartbeat release (needs a SyncBoard)

6. **I2C scan.** Connect to a working SyncBoard, `attachLed` → `enable` →
   `scanI2c`, and confirm **0x49, 0x50, 0x57** all answer. (A SyncBoard's own
   Ref1 fault is irrelevant here — this board has its own reference/chips.
   The SyncBoard does need R4-3 populated + a data-only USB cable so its
   Teensy actually runs; see [syncboard.md](syncboard.md).)
7. **Heartbeat release.** With the SyncBoard enabled and its heartbeat
   toggling, verify HB on the LED board rises to ~3.3 V and the forced-off
   condition releases.

## 4. Functional per-channel (needs SyncBoard + caution)

8. **Per-channel mux.** Via the PWM (0x50), verify DA (current/optical
   feedback), DB (ADC return select), and EN switch as commanded.
9. **Calibrate into a DUMMY LOAD** — a power resistor rated for the current,
   **never a real LED first**. Calibration sweeps current toward the limit,
   and this board can push 15–25 A. Start with a low max current.
10. **Current-sense check.** At a known setpoint, the sense output should read
    **VL + 0.15 V/A × I**; cross-check against the voltage across the dummy
    resistor (or an inline ammeter).
11. **Optical path / ODChannel.** The main optical-feedback path and the
    separate scatter LED (~50 mA) + transimpedance photodiode (LTC6268) —
    later, with light applied.

## Standing hazards

- **0.2 A / 24 V is idle-only.** LED current comes from the switcher and
  needs real current headroom; do not try to fire an LED at that limit — you
  will just brown-out the switcher.
- **Dummy load before real LEDs.** Always. Start at a low max current.
- **Fans on before sustained current.**
- **Split ground** (GNDN is the noisy LED-return ground, single-point tied
  near the power input) — do not defeat it.
- **MOSFET recently swapped** to STP36NF06L (higher input capacitance); the
  designer flags its switching speed as unproven. Scope the gate / channel
  current during any timed or fast switching once driving a load.

## Known-good reference values

| Quantity | Expected |
|----------|----------|
| Rails | 6V5 (VLed), 5 V, 3V3 |
| VL | ~0.30 V |
| 3V3Ref | 3.3 V |
| Current sense | VL + 150 mV/A (3 mΩ × 50) |
| Idle @ 24 V | small (fits within a 0.2 A limit) |
| I2C scan (attached) | 0x49, 0x50, 0x57 |
| Safe state (no HB) | all channel outputs 0 V |
