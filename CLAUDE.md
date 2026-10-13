Circuit schematics and PCB layout are in hardware
Source code is in src
The SyncBoard design and firmware are proven (I've built it before), so rule out design problems there. The Screener board (hardware/ScreenerMobo) and the AirSensor and FieldSensor boards are new designs: when something there doesn't work, consider design errors too, and check the netlist against the datasheets (as with ADC2's IOVDD pin). I have a scope so I can probe anywhere on the board.

## Working rules

- Hardware tests must not be blocked by a failing or missing part: test each
  part independently wherever possible. Firmware setup reports which chips
  answered instead of faulting, and each command needs only its own chip.
  Test scripts skip only what cannot work without a missing chip, and record
  one failure per channel or reading, then carry on.
- Firmware and the Python client share the wire protocol and change together.
  Bump `kFirmwareVersion` (src/State.h) when commands change; the board then
  has to be reflashed.
- Ask before running anything that drives hardware (RF output, switches,
  rails). Read-only queries (SyncBoard `ping`/`status`, SynthNV model/serial)
  are fine.

## Toolchain

- PlatformIO is not on PATH: `~/.platformio/penv/bin/pio run` builds,
  `... run -t upload` flashes.
- Unit tests: `uv run --group dev pytest` (no hardware). `.venv` has pyserial
  only (no numpy or matplotlib).
- USB ids: SyncBoard Teensy 16C0:0483; Windfreak SynthNV Pro 0483:A3E7
  (unit 671).
- Windfreak driver: `../SynthNVProDriver`. Put the parent directory on
  sys.path and import `SynthNVProDriver.nvprocontroller`. Known quirks:
  `get_calibration_succesful()` always returns True (bool of a string),
  `get_pll_enable()` always returns False, and readings were inconsistent
  after `set_pll_enable(True)` without re-sending the frequency.

## Reading the Altium files

`*.SchDoc` and `*.PcbDoc` are OLE compound files (`olefile`).

- Netlist: take it from the PcbDoc. `Nets6/Data` and `Components6/Data` are
  length-prefixed `|KEY=VALUE|` text records. `Pads6/Data` is binary: a type
  byte, then six length-prefixed subrecords; the pad name is a Pascal string
  in the first, and the net and component indices are uint16s at offsets 3
  and 7 of the fifth. Repeated-channel parts share a SOURCEDESIGNATOR, so key
  them by the last element of SOURCEHIERARCHICALPATH (e.g. `SWP1_SWP3`).
- Pin names, part values and the designer's notes: the SchDoc `FileHeader`
  stream, the same text records. Pins are binary records (top byte of the
  length set). RECORD=28 text frames hold the design notes.
- Silkscreen text: PcbDoc `Texts6/Data`.
- `hardware/SyncBoard1.SchDoc` is identical to `schematics/SyncBoard1.SchDoc`.
  `ReferenceDrivers/` (added 2026-09-30) has not been fully reviewed; its
  ADS124S08 driver is the source of the RTD settings, and its SHT45 driver
  is for the digital part, not the analog SHT40I used here.

## Screener board (hardware/ScreenerMobo)

Driver in src/Screener.{h,cpp}, Python `sb.screener`. The full control map
and probe points are in docs/bringup/screener-board.md.

- It sits in Expansion 2, which is SyncBoard H4 (H3 is wired the same for
  the pins it uses), and needs its own 12 V on H2.
- The heartbeat crosses on Iso-2, the same isolator as the SPI lines. It
  becomes ENWD, which enables the power switches and the 5 V analog and RF
  regulators, and its inverse is the PCA9685 output enable.
- CS1 (Teensy pin 0) must idle high (set in `io::configure`), or the
  ADS124S08 takes the SyncBoard DAC's SPI traffic as commands.
- The RF switch is GPIO31 (Teensy pin 14), unshifted. It needs SyncBoard
  J1-22 on pins 1-2; the SyncBoard checkout's gpio section moves it.
- Indicators along the top edge, top to bottom: LL-5 "Power (12V)", LL-6
  "SyncBoard HB" (ENWD), LL-4 "Sensors On", LL-3 "Fluxgate On", LL-2
  "RF Monitor On" (RF_Enable, PCA9685 channel 1).
- Checked against the datasheets (2026-09-30), not yet on hardware:
  - Sw1 is a TMUX1136 (not a TS5A23157). SEL high connects SxA to Dx, which
    puts the IDAC on D+, so the firmware's RTD sign is right.
  - The ADS124S08's registers, commands and timing in the firmware match
    TI SBAS660C. Its section 10.1.4 says PGA_EN = 10 bypasses the PGA, but
    the register table says 00 (10 is reserved). The firmware uses 00, as
    the reference driver does.
- Design gap, from the ISO676x datasheet: Iso-2 (ISO6761, non-F) outputs go
  high when its input side is unpowered or an input is open. If the
  SyncBoard loses power, or H1 is unplugged, while the screener has 12 V,
  ENWD goes high and the PCA9685 (on the screener's always-on 3.3 V) keeps
  its outputs. So the interlock covers a SyncBoard crash or disable, not its
  power loss. The ISO6761F is pin-compatible and defaults low.

## Sensor boards (hardware/FieldSensor, hardware/AirSensor)

Drivers in src/FieldSensor.{h,cpp} and src/AirSensor.{h,cpp}, Python
`sb.screener.field_sensor` and `sb.screener.air_sensor`. The pin map,
transfer functions and probe points are in docs/bringup/sensor-boards.md.

- The FieldSensor flex plugs into the AirSensor's Con-1, and the AirSensor's
  cable into the screener's Con_S. Both run straight through, pin for pin.
- FieldSensor: DRV425 fluxgate (15 ohm shunt, 0.732 V/mT, +-2 mT, REFOUT =
  VCC/2), DRV5055Z3 Hall (25 mV/mT at 5 V, ratiometric) with its supply
  Kelvin-sensed onto REF1, and a PT1000 on RTD channel 7. AirSensor: the
  analog SHT40I-HD1F (outputs 10-90 % of its 5 V) and a PT1000 on RTD
  channel 8.
- Every reading that needs the fluxgate or sensors rail powers it only for
  that reading, inside the firmware command (`screener::PoweredRail`), and
  waits a settle time first: fluxgate 500 ms, Hall and RTD 20 ms, humidity
  300 ms. A rail held on with `setScreenerEnable` stays on. The user wants
  this because the fluxgate heats the head.
- Hall and humidity are read against AIN7 with REF1 as the reference, not
  against AIN5 as the sheet's note says. This cancels 2V5_Offset and the
  supply.
- The mobo wires the sensor boards' S (sense) RTD pair to its drive mux and
  the D pair to its sense mux. This is harmless: polarity still matches.
- The fluxgate settle is measured (2026-09-30), not taken from the
  datasheet. BYP on the ADM7150 has 122 uF (C6-18 22 uF plus CE3-8, a 100 uF
  electrolytic), so the rail rises in ~150 ms. VOUT - REFOUT is within ~1 mV
  (1.4 uT) by ~0.5 s. At the old 100 ms, REFOUT was ~1.3 V and failed the
  check.
- The ADS124S08's reference buffers are off for the internal reference (the
  datasheet's advice, as the reference driver does) and on for REF0/REF1.
- Checked against the datasheets (2026-09-30), not yet on hardware:
  - The ADP7118 (sensors rail, SS open) starts in 380 us.
  - AIN7 as the negative input while it is REFN1. The datasheet shares the
    pin with no restriction. Still unconfirmed on the bench.
  - The REFOUT check (1.4-1.9 V) reads AIN3 against the internal reference,
    so it doesn't depend on 2V5_Offset. DRV425 REFOUT is 45-55 % of VDD. The
    HallP check is off by 2V5_Offset's error (tens of mV), well inside its
    0.5 V margin.

## This build, as of 2026-09-30

- SPI ADC: answers since later on 2026-09-30. The PcbDoc still puts ADC2
  pad 15 (IOVDD) on GND. Setup missed it once in about 15 tries (read 0x00).
  Its readings check out: DVDD reads 3.301 V against the internal reference,
  and AIN5 (2V5_Offset = AVDD/2) agrees with the AVDD monitor within 3 mV.
  AVDD takes ~1 s after enable to rise from 4.78 V to 5.02 V.
- Fluxgate: unpowered until the user fixed a capacitor (2026-09-30). It now
  reads REFOUT 1.64 V and about +0.17 mT.
- Hall and humidity, still open (2026-09-30): the sensors rail does not
  reach the AirSensor. With it on, nothing moves: HallP ~0.72 V (the offset
  dividers leaking into an unpowered 5 V net), RH floating at 2V5_Offset,
  SHT40I T output ~1.0 V. SensorsEnable works, because the RTD muxes it
  enables read RTD 8 (1094 ohm). The user measured Con_S pin 6 at 4.9 V and
  the cable's far end at 2.38 V, so VReg-6 and CF-6 are fine and the break
  is in the Samtec cable or its mating. No sensor line moves with the rail,
  so the far end is open, not a high-resistance contact. By the ADC, Con_S
  pin 10 (RH) also floats and RTD 7 (pins 11/13/15/17, and the flex) reads
  open. Pins 1-3, 5, 7-9 and 12-18 are connected. The cable maps pad n to
  pad n: the fluxgate's 3.3 V (pin 3) and GND (pin 4) arrive, and a row or
  end flip would not deliver them. Neither LSHM footprint marks pin 1, and
  the Con_S and Con1 labels sit at opposite ends. The pad map and probe
  points are in sensor-boards.md. The 2.38 V is probably a neighbouring
  2V5_Offset-biased pin. The sensors rail changes nothing the ADC sees
  (SHT40I T included), so the AirSensor 5 V net really is unfed.
- RTD 7, 2026-09-30: after the user removed a short under the AirSensor
  connector (D1/S1/D2 bridged), it reads exactly 0 in both polarities with
  the rail held on. With the IDAC off, AIN10 stays >= 2.5 V. At polarity 1
  the RTD sits at 0 V apart from AIN10; at polarity 0 it floats up with
  AIN10. So the drive path through Mux2 S7A (pin 25, DRTDP7, CF-13,
  RTD_S1_Con) is open. The user's continuity reaches the CF filters, so the
  suspects are the Mux2 pin 25 joint, CF-13 and S7A. An open drive wire
  saturates the PGA the same way in both global-chop phases, so the firmware
  reports ~0 ohm instead of a fault. The user fixed the open path the same
  day. RTD 7 now reads ~1098 ohm (RTD 8: 1094), and the two polarities agree
  within ~0.7 ohm. It rose ~4 ohm (~1 C) over a minute of readings, more with
  the sensors rail held on, possibly the Hall sensor warming the head.
- RF 1 sweep, 200-800 MHz at -10 dBm (`rf1_sweep_20260930-102713.csv`):
  - The response is within 3 dB of its peak (about 380 MHz) from about 310
    to 665 MHz.
  - There is a notch at 255 MHz, and the level rises again below 250 MHz,
    possibly generator harmonics.
  - The nominal AD8318 dBm reads at least 12 dB above what can reach the
    detector, so only relative levels are meaningful.
  - RF 1 reads live (about 1.13 V) with the generator off; not explained.
