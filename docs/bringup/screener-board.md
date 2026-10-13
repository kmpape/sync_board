# Screener board bring-up

Bring-up for the screener motherboard ("ScreenerMobo", Altium project in
`hardware/ScreenerMobo/`). It plugs into the SyncBoard's Expansion 2 header
(H4; H3 is wired the same for every pin this board uses) and carries seven
high-power load switches, four RF power detectors and two gain/phase
detectors, eight 4-wire RTD inputs, conditioning for the sensor boards on
Con_S (the FieldSensor's fluxgate and Hall sensor, the AirSensor's humidity
sensor; see [sensor-boards.md](sensor-boards.md)), and five LED bar
displays.

You need its 12 V supply, a working SyncBoard, a multimeter and a scope.

## How the board works

Power comes in at 12 V on H2 (the isolated brick). The 12 V rail feeds the
power switches, the ±5 V converter for the RF switch (HP-4), and a 6V5
switcher that feeds the LDOs. The digital 3.3 V (VReg-5) is always on.
Everything else follows either the heartbeat or a PCA9685 output:

| Rail | Regulator | Switched by |
|------|-----------|-------------|
| 3.3 V digital (isolators' board side, PCA9685, both ADCs' digital supply) | VReg-5 | always on |
| 5 V analog (ADS124S08 AVDD, muxes, op-amps, Hall conditioning) | VReg-3 | heartbeat (ENWD) |
| 5VRF (RF detectors) | VReg-2 | heartbeat (ENWD) |
| 5V_Sensors (flex) and 5V_Messy (LED displays) | VReg-6 / VReg-7 | SensorsEnable |
| 3V3_Fluxgate (flex) | VReg-4 | FluxgateEnable |

Everything from the SyncBoard crosses an isolator:

- I2C through Iso-1 (ISO1640): PCA9685 at 0x51 and ADS7828 at 0x4B.
- SPI through Iso-2 (ISO6761): the ADS124S08 on CS1 (Teensy pin 0).
- Two more lines on Iso-2: the heartbeat HB becomes ENWD, and GPIO31
  (Teensy pin 14) becomes the RF switch control. **If Iso-2 is not fitted,
  the heartbeat never reaches the board**: ENWD stays low, so the switches,
  the 5 V analog and RF rails stay off, and the PCA9685's outputs are held
  off by its OE pin even though it still answers on I2C.

The heartbeat is the interlock. ENWD enables every power switch and the 5 V
analog and RF regulators. N1 inverts it into HB\ on the PCA9685's output
enable, so all PCA outputs drop to 0 when the SyncBoard stops. The firmware
also zeroes the PCA9685 on every enable and disable, so nothing comes back
on by itself.

**The interlock only works while the SyncBoard is powered.** Iso-2 is the
non-F ISO6761, whose outputs go high when its input side (3V3_IN, from the
SyncBoard on H1 pin 6) is unpowered, or an input is open (datasheet function
table). If the SyncBoard loses power, or the H1 cable comes out, while the
screener has 12 V, ENWD goes high and HB\ low. The PCA9685 runs from the
screener's always-on 3.3 V, so it holds its outputs: anything that was on
stays on. A crash or `disable` is covered, since the SyncBoard's watchdog
then drives HB low. The ISO6761F is pin-compatible and defaults low, which
would make ENWD drop in these cases too. With the F part, CS, SCK, MOSI and
the RF switch control would also default low.

PCA9685 outputs (firmware channel n = LED(n-1)):

| Channel | Net | Drives | Indicator |
|---------|-----|--------|-----------|
| 1 | RF_Enable | AD8318 detectors 1-3 (ENBL) | LL-2 |
| 2 | RF_Enable2 | AD8318 detector 4 (spare input) | none |
| 3-9 | SWEN1-7 | power switch 1-7 input | LL-1 of that channel |
| 10 | — | unconnected | |
| 11 | FluxgateEnable | 3V3_Fluxgate regulator | LL-3 |
| 12 | SensorsEnable | 5V_Sensors + 5V_Messy regulators, RTD mux EN | LL-4 |
| 13 | RTD_Polarity | Sw1: which RTD drive wire gets the excitation | none |
| 14-16 | MX1-MX3 | RTD mux address A0-A2 (channel - 1) | none |

The other two indicators in the top-edge column are LL-5 (12 V in) and
LL-6 (ENWD, the isolated heartbeat).

The power switches are Infineon HITFET low-side switches. Each channel's
load goes between HP-1 pin 1 (12 V) and pin 2 (VL), and "on" pulls VL to
ground. A Schottky clamps VL to 12 V. The switches PWM at about 200 Hz (the
PCA9685 runs its slow clock, as on the SyncBoard).

RF: Con1 bore pickup, Con2 forward power, Con3 backward power, Con4 spare,
each through a ~480 MHz bandpass filter and splitters. The **maximum input
is 4 dBm**. ADS7828 channels:

| Ch | Net | Source |
|----|-----|--------|
| 1 | RF1_Mag | AD8318 on Con1 |
| 2 / 3 | RF12_Mag / RF12_Ph | AD8302 #1, Con1 vs Con2 |
| 4 | RF2_Mag | AD8318 on Con2 |
| 5 / 6 | RF23_Mag / RF23_Ph | AD8302 #2, Con2 vs Con3 |
| 7 | RF3_Mag | AD8318 on Con3 |
| 8 | RF4_Mag | AD8318 on Con4 (needs RF_Enable2) |

The ADS7828's reference is AD8302 #1's 1.8 V VREF (through R5-1), so it
reads 0-1.8 V, and only while the heartbeat has 5VRF up.

ADS124S08 inputs, following the "ADC Setup" note on the sheet:

| Measurement | Inputs | Reference | Notes |
|-------------|--------|-----------|-------|
| RTD 1-8 | AIN0-AIN1 via Mux1 | REF0 (4.7 kΩ resistor) | 500 µA IDAC on AIN10 via Sw1 and Mux2, gain 2, global chop, 16 ms delay |
| Fluxgate | AIN2-AIN3 | internal 2.5 V | ±1.65 V span |
| Hall (B) | AIN4-AIN7 | REF1 = AIN6/AIN7 | (HallS − HallN)/(HallP − HallN): ratiometric to the Hall supply |
| T_RH / RH | AIN8 / AIN9 vs AIN7 | REF1 | ratiometric to the flex 5 V; or vs AIN5 against the internal 2.5 V |

AIN5 is 2V5_Offset, half the 5 V analog rail (RD-4, buffered by Opa2-4).
BConditioner halves HallS, HallP, HallN, T_RH and RH about it, so each input
reads (signal + 2V5_Offset)/2. The sheet's note reads the Hall and humidity
signals against AIN5. The driver reads them against AIN7 instead, which
cancels 2V5_Offset: see [sensor-boards.md](sensor-boards.md). RTD channels
1-6 are the HRTD headers: pin 1 D+, pin 2 D-, pin 3 V+, pin 4 V-. Wire D+ and
V+ to one end of the RTD, D- and V- to the other. Channel 7 is the
FieldSensor's PT1000 and channel 8 the AirSensor's, both through Con_S. The
front end is sized for PT1000s (900-1700 Ω, full scale 2350 Ω).

The RF switch control runs from Teensy pin 14 straight to the expansion
header (no level shifter), provided **SyncBoard jumper J1-22 is on pins 1-2**.
The SyncBoard checkout's `gpio` section moves it to the PWM0 side. Iso-2 and
Buf1 turn it into 5 V TTL on the centre pin of RFS1. With the system
disabled, pin 14 goes high-impedance, so Iso-2 outputs its default for an
open input, which is high for the non-F ISO6761: RFS1 sits at 5 V while
the system is disabled. Scope RFS1 once to confirm, if the RF path matters
then.

## Firmware and scripts

Flash firmware 2.2.0 or later (`pio run -t upload`); 2.1.0 has no sensor
board readings. `setupScreener` probes
the three chips independently and never fails for a missing one: it logs a
warning and reports what it found. Every command needs only its own chip.
The scripts follow the same rule, so a partly populated board (for example
with no SPI ADC) can still be tested piece by piece.

```sh
python hwtest/screener_leds.py       # first light: the five top-edge indicators
python hwtest/screener_board.py      # interactive test menu
```

## 0. Before power

- Supply off, board unplugged: check continuity from GND to 12 V, 6V5, 5 V
  and 3.3 V. There should be no shorts.
- Check the H2 pinout against the housing: pins 1-2 GND, 3-4 +12 V.
- Jumpers: J1-1/J1-2 pick which AD8302 feeds the RFB/RFC display bars
  (default RF12). J1-3 picks the bar reference (2.5 V for the linear
  LM3914s, 1.8 V for the log LM3915s).

## 1. Power and first light

Plug the board into Expansion 2 and power H2 with a current limit. With
the SyncBoard powered but not enabled, LL-5 (12 V) lights at once and nothing
else should. Note the idle current. (With the SyncBoard unpowered, Iso-2's
default output state decides ENWD. The non-F ISO6761 defaults high, so LL-6
lights then: see the interlock note above.)

Run `screener_leds.py`. It attaches and enables the board, then turns on
the three software-driven indicators:

| LED | Expect | If it stays dark |
|-----|--------|------------------|
| LL-5 | lit (12 V) | H2 power, F-1, TVS1 |
| LL-6 | lit once enabled (ENWD) | heartbeat path: SyncBoard HB, H1 pin 14, Iso-2 D channel, 3V3_IN on H1 pin 6 |
| LL-4, LL-3, LL-2 | lit | PCA9685 OE (pin 23) should be ~0 V with LL-6 lit; outputs on pins 18, 17, 6 |

If the script reports the PCA9685 missing, the I2C side is not reaching the
board. Check Iso-1, the 3.3 V digital rail, and H1 pins 11/13.

## 2. The test menu

`screener_board.py` shows a menu. Pick a test, review its options
(Enter runs with them; `name=value` changes one), and you return to the menu
afterwards. Each test ends with all outputs off. Tests whose chip did not
answer at setup are skipped with the reason; the rest still run. A sensible
order:

1. `link`: LL-5, LL-6 with enable, the I2C scan (0x51, 0x4B), and LL-6
   dropping on disable (the interlock).
2. `chips`: re-probes each chip and reads the ADS124S08 supply monitors
   (AVDD ≈ 5 V proves the heartbeat-gated analog rail). Set `fitted` to the
   chips your build has, e.g. `fitted=pca9685,ads7828` without the SPI ADC.
3. `enables`: each enable with its indicator, plus a meter on the switched
   flex rails at Con_S pins 3 and 6.
4. `switches`: each channel's LL-1 and VL, steady or PWM. A small load
   makes the VL check clear; without one, VL floats when off.
5. `rf-switch`: RFS1 at 5 V / 0 V, or a square wave for the scope. It also
   checks HP-4 at ±5 V. It needs no chip at all.
6. `rf-adc`: the eight detector outputs. With no RF, the AD8318 outputs
   sit at the top of their range (often the ADC's 1.8 V full scale) and the
   AD8302 outputs wander. Then feed a generator (≤ 4 dBm, near 480 MHz) into
   each input in turn and watch the matching channels move.
7. `rtd`: with a known resistor wired 4-wire on each channel (1 kΩ 0.1 %
   reads 0 °C on a PT1000), set `expect=1000` for automatic checks. The
   default reading is chopped in firmware (six conversions, excitation
   reversed between each, ~1.1 s per channel). Each reading powers the
   sensors rail (the muxes' enable), so LL-4 flashes. `polarity=both` reads each
   polarity once instead: they should agree, and the offset it prints is the
   wiring thermal EMF that chopping cancels. A negative reading means the V
   pair is swapped against the D pair; an over-range fault means an open
   channel.
8. `field`, `air`: the FieldSensor (Hall, fluxgate, RTD 7) and the
   AirSensor (humidity sensor, RTD 8) on Con_S. Each reading powers its
   rail only while it runs. Without the SPI ADC both ask for meter readings
   instead. See [sensor-boards.md](sensor-boards.md).
9. `adc`: any ADS124S08 input pair, with statistics.
10. `displays`: the LED bars, with a magnet at the Hall sensor.
11. `pwm`: raw access to one PCA9685 output, for probing.

## Known-good values

| Quantity | Expected |
|----------|----------|
| I2C scan (screener part) | 0x4B, 0x51 |
| ENWD (N1 pin 2) with the SyncBoard enabled | ~3.3 V; PWM1 pin 23 ~0 V |
| ADS124S08 AVDD / DVDD monitors | ~5.0 V / ~3.3 V |
| Switch PWM | ~200 Hz |
| RF ADC full scale | 1.8 V (AD8302 #1 VREF) |
| RTD excitation / reference | 500 µA / 4.7 kΩ (≈ 2.35 V across it) |
| RFS1 control | 0 / 5 V TTL |
