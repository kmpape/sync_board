# Sensor boards bring-up (AirSensor and FieldSensor)

Bring-up for the two sensor boards the screener board reads: the
FieldSensor flex (`hardware/FieldSensor/`) and the AirSensor board
(`hardware/AirSensor/`). Neither has a chip with a digital interface. The
screener's ADS124S08 reads every signal, and the screener's PCA9685
switches their power. Bring the screener board up first
([screener-board.md](screener-board.md)), at least its `link`, `chips` and
`enables` tests.

## How they connect

```
FieldSensor flex ──12-way flex── AirSensor Con-1
                                 AirSensor Con1 ──20-way cable── ScreenerMobo Con_S
```

- The FieldSensor sits on the axis between the resonator and the magnet
  face, so it has almost no magnetic parts. Its flex exits to the right and
  turns 90° into Con-1 on the AirSensor, contacts facing down.
- The AirSensor sits in the airflow and carries the main connector (Con1,
  Samtec LSHM-110). The designer's note says to connect it to the **black end
  of the cable**.
- Both connectors run straight through, pin for pin, to the screener's
  Con_S:

| Con_S | Screener net | Sensor side | What |
|------:|--------------|-------------|------|
| 1 | FluxgatePRaw | BOut1 | DRV425 VOUT |
| 2 | FluxgateNRaw | BOut2 | DRV425 REFOUT (VCC/2) |
| 3 | 3V3_Fluxgate_Con | VCC_FluxGate | fluxgate supply (fluxgate enable) |
| 4, 19, 20 | GND | GND | three returns, to cut the ground drop |
| 5 | HallN | GND_Hall | Hall ground, sensed at the sensor |
| 6 | 5V_Sensors_Con | 5V | Hall and SHT40I supply (sensors enable) |
| 7 | HallS | HallOut1 | DRV5055 output |
| 8 | T_RH_Con | T_RH | SHT40I temperature output |
| 9 | HallP | VCC_Hall_Out | Hall supply, sensed at the sensor |
| 10 | RH_Con | RH | SHT40I humidity output |
| 11, 13, 15, 17 | RTD_D2, _S2, _S1, _D1 | FieldSensor PT1000 | RTD channel 7 |
| 12, 14, 16, 18 | RTD_Air_D1, _D2, _S1, _S2 | AirSensor PT1000 | RTD channel 8 |

The cable maps pad n to pad n. On the bench (2026-09-30) the fluxgate's
3.3 V on pin 3 and ground on pin 4 reach the flex; a cable that swapped the
rows or reversed the ends would put pin 3 on ground or on an RTD line.

Neither LSHM footprint marks pin 1: the silkscreen is four corner ticks. The
designators sit at opposite ends: "Con_S" by pins 19/20, "Con1" by pins 1/2.
Seen from the top, as laid out:

```
ScreenerMobo Con_S (bottom board edge below)    AirSensor Con1 (flex connector Con-1 above right)
"Con_S"                                         "Con1"
  20 18 16 14 12 10  8  6  4  2  inner row        1  3  5  7  9 11 13 15 17 19
  19 17 15 13 11  9  7  5  3  1  edge row         2  4  6  8 10 12 14 16 18 20
                              ^ pin 1           ^ pin 1
```

Easier probe points than the 0.5 mm pads:

| Net | ScreenerMobo | AirSensor |
|-----|--------------|-----------|
| 5 V (pin 6) | CF-6 pad 1 (connector side); C5-19 (regulator side) | C1-1 pad 1 (0805, below Con1); C2-1 pad 1 (0402, right edge); RH1 pin 3 (bottom side) |
| 3.3 V fluxgate (pin 3) | CF-5 pad 1 | — (on the flex) |

On the screener, pins 5 and 7–10 are biased to 2V5_Offset through
2 × 100 kΩ (the BConditioner dividers). With nothing driving them from the
far end, a meter reads about 2.4 V on each.

The sensor boards call their RTD wires D (drive) and S (sense). The screener
wires the S pair to its drive mux and the D pair to its sense mux. That makes
no difference: each pair has one wire at each end of the RTD, with matching
polarity, so both channels read positive.

## What each sensor is

| Sensor | Board | Supply | Read as | Converts with |
|--------|-------|--------|---------|---------------|
| DRV425 fluxgate | FieldSensor | 3V3_Fluxgate | AIN2 − AIN3, internal 2.5 V ref | 12.2 mA/mT × 15 Ω × 4 V/V = **0.732 V/mT**, ±2 mT |
| DRV5055Z3 Hall | FieldSensor | 5V_Sensors | AIN4 − AIN7 against REF1 (AIN6/AIN7) | (OUT − GND)/(VCC − GND) − 0.5, × **200 mT** (25 mV/mT at 5 V, ratiometric), ±85 mT |
| SHT40I-HD1F RH/T | AirSensor | 5V_Sensors | AIN9 / AIN8 − AIN7 against REF1 | RH = −12.5 + 125·V/VDD, T = −66.875 + 218.75·V/VDD |
| PT1000 | FieldSensor | (RTD excitation) | RTD channel 7 | `pt1000_celsius` |
| PT1000 | AirSensor | (RTD excitation) | RTD channel 8 | `pt1000_celsius` |

Notes:

- **Fluxgate.** The DRV425 runs in ratiometric mode (RSEL1 high), so REFOUT
  is VCC/2 ≈ 1.65 V. Its output VOUT − REFOUT depends only on the field, not
  on the supply. The 60.4 Ω R3-1 in series with REFIN is the datasheet's
  dummy shunt, which keeps the shunt amplifier's gain at 4. The OR and ERROR
  flags are not connected, so the only sign of an out-of-range field is VOUT
  near its rail. The driver raises beyond ±1.5 V (≈ ±2 mT).
- **Hall.** The sensor's own supply comes back on two sense lines (HallP,
  HallN) through 100 Ω each. BConditioner halves HallS, HallP and HallN about
  2V5_Offset, and HallP/HallN become the ADC's REF1. Reading HallS against
  HallN with REF1 as the reference gives (OUT − GND)/(VCC − GND) exactly:
  2V5_Offset cancels, and so does the supply, because the DRV5055's zero and
  sensitivity both scale with VCC. Positive field is flux leaving the marked
  face of the package (a south pole in front of it).
- **Humidity.** The SHT40I-HD1F is the analog variant: RH and T each come out
  as 10–90 % of its supply. The driver reads them against REF1, as for the
  Hall sensor. The supply sense is at the FieldSensor, on the same 5 V net as
  the SHT40I, so the reading is ratiometric. The cable and flex drops between
  the two boards are a few mV (a few 0.1 °C at most). Without the FieldSensor
  there is no REF1, so the `offset` reference reads against 2V5_Offset and
  assumes the nominal 5 V supply. At room conditions each 1 % of supply error
  then costs about 0.9 °C and 0.6 %RH.

## Power: on for each reading only

The DRV425 draws 6 mA plus its compensation current, up to ~30 mA when the
field drives the coil to its rail, and it heats the head. The PT1000 next to
it on the flex sees that heat. So every reading powers the rail it needs only
while it runs. The firmware switches the rail on, waits a settle time, runs
the conversions, and switches the rail off again, all inside one command.
A host that crashes mid-reading cannot leave the fluxgate on.

| Reading | Rail (indicator) | Default settle | Why |
|---------|------------------|----------------|-----|
| fluxgate | fluxgate (LL-3) | 500 ms | ADM7150 rises in ~1.2 ms per µF of BYP, and BYP has 122 µF (C6-18 22 µF + CE3-8 100 µF): ~150 ms. Measured: VOUT − REFOUT within ~1 mV (1.4 µT) by ~0.5 s |
| Hall, flex supply | sensors (LL-4) | 20 ms | ADP7118 (SS open) starts in 380 µs, the DRV5055 in well under a ms |
| humidity | sensors (LL-4) | 300 ms | SHT40I: first measurement at 7–15 ms, then 200 ms for its outputs to settle |
| RTD 1–8 | sensors (LL-4, the muxes' enable) | 20 ms | the mux enable is immediate |

Each command takes a settle override (`settle_ms` in Python). A rail switched
on with `set_enable` stays on through readings, which only wait out whatever
settle time is left. Use that for continuous monitoring or for the LED
displays. The sensors rail also powers the Hall sensor and the displays, so
LL-4 and the bars flash with every RTD reading.

The fluxgate settle default comes from a bench trace (2026-09-30): after
switch-on, REFOUT reached 1.51 V at 0.16 s, 1.61 V at 0.33 s and 1.64 V by
0.65 s. VOUT − REFOUT was 11 mV low at 0.16 s, 4 mV at 0.33 s, 1.3 mV at
0.49 s and within 0.5 mV from 0.65 s. At the old 100 ms default, REFOUT
was still ~1.3 V and failed the check. The slow start is CE3-8, the 100 µF
electrolytic on the ADM7150's BYP pin. Removing it would bring the rise
down to ~26 ms, at the cost of more low-frequency noise on the rail. The
`field` test's `sweep=yes` measures the figure again.

A fluxgate reading first checks that REFOUT sits near 1.65 V. A Hall or
humidity reading first checks that the Hall supply sense reads at least
4.5 V. Without these checks, an unplugged flex or a dead rail would read as
zero field or as noise, instead of raising with the reason.

## 0. Before connecting

The boards are passive, so check them before they meet the screener:

- FieldSensor, at its flex contacts: pin 12 (5V) and pin 9 (VCC_FluxGate)
  show no short to pin 1 (GND). Pin 2 to pin 5 (D1 to D2) reads ~1.5 kΩ:
  the PT1000 plus two 100 Ω resistors in each wire. Pin 3 to pin 4 (S1 to
  S2) reads the same. Pin 2 to pin 3 reads ~400 Ω, because D1 and S1 meet at
  one end of the RTD.
- AirSensor, at Con1: pin 12 to pin 14 reads ~1.1 kΩ (its PT1000 at room
  temperature; no series resistors on this board). With the flex plugged
  in, pin 17 to pin 11 reads ~1.5 kΩ.

## 1. Power

With the screener set up, run its `enables` test with `rails=yes`. Con_S
pin 3 should be ~3.3 V with the fluxgate enable on, and pin 6 ~5 V with the
sensors enable on. Measure again with the sensor boards plugged in: the
rails should hold their voltage with the sensors loading them.

## 2. The tests

Both are in the screener test menu (`python hwtest/screener_board.py`).

`field`:
1. Reads the flex 5 V at the Hall sensor (4.5–5.25 V).
2. Runs rounds of Hall, fluxgate and RTD 7 readings, sensors-rail readings
   first and the fluxgate last. With no magnet near, the Hall should read
   within ~3 mT of zero (its zero is 2.43–2.57 V at 5 V), and the fluxgate
   within ~0.1 mT (Earth's field is ~0.05 mT). A magnet moves both.
3. Asks whether LL-4 and LL-3 lit only during the readings.
4. `sweep=yes` steps the settle time with the rail off for `rest_s` before
   each reading. It checks that readings from the firmware's default on
   agree with the longest settle's.
5. `heat_s=60` holds the fluxgate on for a minute and then off for a
   minute, reading RTD 7 throughout. It shows how much the fluxgate warms
   the head.

`air`:
1. Reads the SHT40I against the flex supply (`reference=auto` falls back to
   `offset` without the FieldSensor), and RTD 8.
2. Checks RH is within 0–100 % and T within 10–40 °C.
3. Checks the SHT40I's T agrees with RTD 8 within `tol` (default 1 °C):
   they sit on the same small board in the same air.
4. `reference=both` shows the flex and offset readings side by side. Their
   difference is the error of assuming the nominal supply.
5. `sweep=yes` checks the 300 ms default settle.

Without the SPI ADC both tests fall back to a meter. They hold the rails on
and ask for the voltage at the C8 caps on the ADC side of the conditioning
(the other pad of each is GND), then convert what you type in:

| Cap | Net | Expect |
|-----|-----|--------|
| C8-6 | FluxgateP (VOUT) | REFOUT ± 1.5 V (0.732 V/mT) |
| C8-7 | FluxgateN (REFOUT) | ~1.65 V |
| C8-8 | HallRefP = (HallP + 2.5)/2 | ~3.75 V |
| C8-9 | HallRefN = (HallN + 2.5)/2 | ~1.25 V |
| C8-10 | HallSignal = (HallS + 2.5)/2 | ~2.5 V, 12.5 mV/mT |
| C8-13 | T_RH = (V_T + 2.5)/2 | ~2.2–2.35 V at 18–28 °C |
| C8-14 | RH = (V_RH + 2.5)/2 | ~2.1–2.7 V at 30–60 %RH |

## From Python

```python
field = sb.screener.field_sensor
field.read_fluxgate_mt(count=4)   # fluxgate on, 500 ms, 4 conversions, off
field.read_hall_mt()              # sensors rail on, 20 ms, 1 conversion, off
field.read_supply()               # flex 5 V at the Hall sensor
field.read_temperature_c()        # PT1000 on RTD 7

air = sb.screener.air_sensor
reading = air.read_humidity()     # AirReading: relative_humidity, temperature_c, ...
air.read_temperature_c()          # PT1000 on RTD 8
```

## Known-good values

| Quantity | Expected |
|----------|----------|
| Fluxgate REFOUT (FluxgateN) | ~1.65 V |
| Fluxgate sensitivity | 0.732 V/mT, ±2 mT |
| Flex 5 V at the Hall sensor | ~5.0 V (4.5 V minimum for the DRV5055 and SHT40I) |
| Hall output at zero field | 0.5 of its supply (2.43–2.57 V at 5 V) |
| Hall sensitivity | 25 mV/mT at 5 V (200 mT per unit of supply) |
| SHT40I outputs | 10–90 % of its supply |
| RTD 7 and 8 at room temperature | ~1080–1100 Ω (20–26 °C) |
