"""High-level SyncBoard API.

Typical use::

    from syncboard import SyncBoard

    with SyncBoard.connect() as sb:          # auto-discovers the port
        sb.initialise(led_board=True)        # attach boards + enable
        sb.leds.set_level(5, 0.1)
        sb.leds.pulse(5, duration_ms=50)

All methods are synchronous: they return (or raise) once the firmware has
executed the command. Firmware-side rejections raise
:class:`syncboard.CommandError` with the firmware's explanation.
"""

from __future__ import annotations

import logging
import math
import time
from dataclasses import dataclass
from enum import IntEnum
from typing import Sequence

from syncboard.protocol import SyncBoardError
from syncboard.transport import Transport

logger = logging.getLogger("syncboard")

# Wire values shared with the firmware (src/Signals.h).
class SignalMode(IntEnum):
    ADC = 0            # record SyncBoard ADC channel <option>
    DAC = 1            # write SyncBoard DAC channel <option>
    MAG_DAC = 3        # write magnet-board DAC channel <option>
    MAG_ADC = 4        # record magnet-board ADC channel <option>
    MAG_HALL_READ = 5  # record Hall sensor <option> in mT
    MAG_CURRENT = 6    # set magnet current (option 1 = NC output)
    MAG_FIELD = 7      # set magnet field in mT via Hall calibration
    DO = 8             # write digital output <option 1..4>
    CONDUCTOR = 9      # advance slave signals in bitmask <option>
    LED = 10           # switch LED <option> (low power only)
    LED_TIMED = 11     # pulse LED <option> for <value> ms
    DO_TIMED = 12      # pulse digital output <option> for <value> ms
    GPIO_WRITE = 13    # write GPIO labelled <option>


class FeedbackMode(IntEnum):
    CURRENT = 0
    OPTICAL = 1


# Wire values shared with the firmware (src/Screener.h).
class ScreenerEnable(IntEnum):
    RF = 0          # AD8318 detectors on RF inputs 1-3 (Con1-Con3)
    RF_SPARE = 1    # AD8318 detector on the spare RF input (Con4)
    FLUXGATE = 2    # 3.3 V fluxgate supply on the sensor flex
    SENSORS = 3     # 5 V flex-sensor and LED-display rails, RTD multiplexers


class ScreenerRef(IntEnum):
    RTD = 0         # REFP0/REFN0: voltage across the 4.7 kOhm RTD reference
    HALL = 1        # AIN6/AIN7: the Hall sensor's conditioned excitation
    INTERNAL = 2    # 2.5 V internal reference


@dataclass(frozen=True)
class Status:
    enabled: bool
    led_board_attached: bool
    magnet_board_attached: bool
    sync_mode: int
    screener_board_attached: bool


@dataclass(frozen=True)
class LedMeasurement:
    current_a: float
    optical_mv: float


@dataclass(frozen=True)
class LedSetup:
    level: float
    current_a: float       # current at the present level
    optical_mv: float      # optical monitor voltage at the present level
    max_current_a: float   # current at level 1.0


@dataclass(frozen=True)
class CameraInputs:
    """Raw state of the camera interface input lines."""

    trigger_ready: bool
    reading: bool
    led: tuple[bool, bool, bool, bool]  # per-frame LED gating lines 1..4


@dataclass(frozen=True)
class ScreenerChips:
    """Which chips answered at screener-board setup. Each is optional: the
    methods that need a missing one raise, the rest keep working."""

    pca9685: bool     # switches, enables, RTD mux + polarity
    ads7828: bool     # RF detector ADC
    ads124s08: bool   # SPI ADC: RTDs, Hall, fluxgate, humidity


@dataclass(frozen=True)
class AdcReading:
    """One screener-board ADS124S08 conversion."""

    code: int      # raw signed 24-bit result
    ratio: float   # V_in / V_ref, -1..1


@dataclass(frozen=True)
class Frame:
    """One frame of an image sequence.

    ``exposure_ms = 0`` means the frame is ended by the camera's LED gating
    signal and is only meaningful with ``led_by_camera=True``; otherwise the
    frame would end immediately.
    """

    led: int = 0             # LED channel lit during the frame; 0 = none
    exposure_ms: float = 0.0


class SyncBoard:
    """Owns the transport and exposes the board through per-subsystem APIs:
    :attr:`leds`, :attr:`magnet`, :attr:`screener`, :attr:`signals`,
    :attr:`io`, :attr:`imaging`.
    """

    ENABLE_TIMEOUT_S = 5.0  # disable blocks ~1 s in firmware

    def __init__(self, transport: Transport):
        self._t = transport
        self.leds = LedApi(transport)
        self.magnet = MagnetApi(transport)
        self.screener = ScreenerApi(transport)
        self.signals = SignalApi(transport)
        self.io = IoApi(transport)
        self.imaging = ImagingApi(transport)

    @classmethod
    def connect(cls, port: str | None = None) -> "SyncBoard":
        """Opens the board (auto-discovering the port if none is given) and
        verifies it answers as a SyncBoard."""
        board = cls(Transport(port))
        board.ping()
        return board

    def __enter__(self) -> "SyncBoard":
        return self

    def __exit__(self, *exc_info) -> None:
        self.close()

    def close(self, disable: bool = True) -> None:
        """Disables the system (best effort) and closes the port."""
        try:
            if disable:
                self.disable()
        except Exception:
            logger.warning("could not disable the system while closing", exc_info=True)
        finally:
            self._t.close()

    def reconnect(self) -> None:
        """Reopens the serial connection, rediscovering the port by USB id
        (the board sometimes re-enumerates on a different path). The board's
        state is unknown afterwards; re-run :meth:`initialise`."""
        self._t.reconnect()
        self.ping()

    # -- system --------------------------------------------------------------

    def ping(self) -> str:
        """Returns the firmware version; raises if this is not a SyncBoard."""
        fields = self._t.request("ping")
        if not fields or fields[0] != "syncboard":
            raise ConnectionError(f"device did not identify as a syncboard: {fields}")
        return fields[1]

    def status(self) -> Status:
        fields = self._t.request("status")
        return Status(
            enabled=fields[0] == "1",
            led_board_attached=fields[1] == "1",
            magnet_board_attached=fields[2] == "1",
            sync_mode=int(fields[3]),
            screener_board_attached=fields[4] == "1",
        )

    def initialise(self, led_board: bool = False, magnet_board: bool = False,
                   screener_board: bool = False) -> None:
        """Convenience bring-up: declare attached boards, enable the system,
        and set up the magnet and screener boards if present. Safe to call
        regardless of the board's current state."""
        self.disable()  # board attachment requires a disabled system
        self.attach_led_board(led_board)
        self.attach_magnet_board(magnet_board)
        # self.attach_screener_board(screener_board)
        self.enable()
        if magnet_board:
            self.magnet.setup()
        if screener_board:
            self.screener.setup()

    def enable(self) -> None:
        """Brings up buses, IO and the heartbeat; most commands need this."""
        self._t.request("enable", timeout=self.ENABLE_TIMEOUT_S)

    def disable(self) -> None:
        """Stops all activity and puts every output into a safe state."""
        self._t.request("disable", timeout=self.ENABLE_TIMEOUT_S)

    def reset_config(self) -> None:
        """Soft reset of all configuration; leaves the system disabled."""
        self._t.request("resetConfig", timeout=self.ENABLE_TIMEOUT_S)

    def factory_reset(self) -> None:
        """Erases the EEPROM, including all LED calibrations."""
        self._t.request("factoryReset")

    def attach_led_board(self, present: bool = True) -> None:
        """Declares the LED expansion board present (system must be disabled)."""
        self._t.request("attachLed", present)

    def attach_magnet_board(self, present: bool = True) -> None:
        """Declares the magnet board present (system must be disabled)."""
        self._t.request("attachMagnet", present)

    def attach_screener_board(self, present: bool = True) -> None:
        """Declares the screener board present in Expansion 2 (system must be
        disabled). It claims GPIO31 for the RF switch."""
        self._t.request("attachScreener", present)

    def scan_i2c(self) -> list[int]:
        """Returns the responding I2C addresses (requires an enabled system)."""
        fields = self._t.request("scanI2c", timeout=10.0)
        return [int(f) for f in fields[1:]]


class LedApi:
    CALIBRATE_TIMEOUT_S = 60.0

    #: Rated maximum current per channel for the LEDs installed on this
    #: microscope. Update when the physical LEDs change; calibrate() refuses
    #: to default a channel that is not listed here.
    MAX_CURRENT_A: dict[int, float] = {1: 10.0, 2: 8.0, 3: 12.0, 4: 8.0, 7: 6.0}

    def __init__(self, transport: Transport):
        self._t = transport

    def calibrate(self, channel: int, max_current_a: float | None = None) -> None:
        """Sweeps the LED to map levels 0..1 onto [turn-on, max current].

        The max current defaults to the channel's rating in
        :attr:`MAX_CURRENT_A`; pass it explicitly only when you know the
        installed LED tolerates it. Run once per newly connected LED; the
        result is stored in EEPROM. Blocks for a few seconds and flashes
        the LED.
        """
        if max_current_a is None:
            if channel not in self.MAX_CURRENT_A:
                raise ValueError(
                    f"no rated current known for LED channel {channel}; "
                    f"pass max_current_a explicitly (and update LedApi.MAX_CURRENT_A)"
                )
            max_current_a = self.MAX_CURRENT_A[channel]
        self._t.request("calibrateLed", channel, float(max_current_a),
                        timeout=self.CALIBRATE_TIMEOUT_S)

    def set_level(self, channel: int, level: float,
                  feedback: FeedbackMode = FeedbackMode.CURRENT) -> None:
        """Sets the calibrated intensity (0..1) used when the LED is on."""
        self._t.request("setLedLevel", channel, float(level), int(feedback))

    def on(self, channel: int) -> None:
        """Turns a LED on until further notice (refused above 30% level;
        use :meth:`pulse` for high power)."""
        self._t.request("switchLed", channel, True)

    def off(self, channel: int) -> None:
        self._t.request("switchLed", channel, False)

    def pulse(self, channel: int, duration_ms: float) -> None:
        """Turns a LED on for ``duration_ms`` (timed by the firmware)."""
        self._t.request("switchLedTimed", channel, float(duration_ms))

    def all_off(self) -> None:
        self._t.request("disableLeds")

    def measure(self, channel: int) -> LedMeasurement:
        """Measures LED current and the optical monitor (takes ~100 ms)."""
        fields = self._t.request("measureLed", channel, timeout=5.0)
        return LedMeasurement(float(fields[0]), float(fields[1]))

    def measure_photodiode(self, channel: int) -> float:
        """One-shot photodiode read in mV (uncalibrated)."""
        fields = self._t.request("measurePhotodiode", channel)
        return float(fields[0])

    def get_setup(self, channel: int) -> LedSetup:
        fields = self._t.request("getLedSetup", channel)
        return LedSetup(*(float(f) for f in fields))


class MagnetApi:
    CALIBRATE_TIMEOUT_S = 30.0

    def __init__(self, transport: Transport):
        self._t = transport

    def setup(self) -> None:
        """Probes the board's chips and powers its DAC. Must be re-run after
        every system enable (``SyncBoard.initialise`` does this for you)."""
        self._t.request("setupMagnet")

    def enable(self, on: bool = True) -> None:
        self._t.request("enableMagnet", on)

    def select_output(self, nc: bool = True) -> None:
        """Routes the current through the NC (True) or NO (False) output."""
        self._t.request("selectMagnetOutput", nc)

    def calibrate(self) -> None:
        """Maps DAC volts to coil current for both outputs (takes seconds,
        drives current through the coil)."""
        self._t.request("calibrateMagnet", timeout=self.CALIBRATE_TIMEOUT_S)

    def calibrate_hall(self, hall_id: int) -> None:
        """Maps coil current to the field measured by one Hall sensor."""
        self._t.request("calibrateHall", hall_id, timeout=self.CALIBRATE_TIMEOUT_S)

    def set_current(self, value: float, nc: bool = True) -> None:
        """Sets the coil current in calibrated units (0 = no current)."""
        self._t.request("setMagnetCurrent", nc, float(value))

    def set_field(self, milli_tesla: float, nc: bool = True) -> None:
        """Sets the field via the Hall calibration."""
        self._t.request("setMagnetField", nc, float(milli_tesla))

    def read_hall(self, hall_id: int) -> float:
        """Returns one Hall sensor's field in mT."""
        fields = self._t.request("readHall", hall_id)
        return float(fields[0])

    def read_adc(self, channel: int) -> float:
        """Raw magnet-board ADC read in volts (channel 1..8)."""
        fields = self._t.request("readMagnetAdc", channel)
        return float(fields[0])

    def set_dac(self, channel: int, volts: float) -> None:
        """Raw magnet-board DAC write (bring-up/debug use)."""
        self._t.request("setMagnetDac", channel, float(volts))


class ScreenerApi:
    """The screener board in Expansion 2 (hardware/ScreenerMobo; the control
    map is in src/Screener.h).

    Everything but :meth:`setup` needs the board set up since the last
    enable; ``SyncBoard.initialise(screener_board=True)`` does that.
    """

    NUM_SWITCHES = 7
    NUM_RTDS = 8
    AINCOM = 12                    # ADS124S08 input code for AINCOM (0-11 = AIN0-11)
    INTERNAL_REF_V = 2.5
    RTD_REF_OHMS = 4700.0          # reference resistor; RTD full scale is half this (gain 2)
    OFFSET_V = 2.5                 # nominal 2V5_Offset (half the board's 5 V rail)

    #: ADS7828 channel -> net on the ScreenerMobo sheet (what drives it).
    RF_CHANNELS: dict[int, str] = {
        1: "RF1_Mag",    # AD8318 on Con1 (bore pickup)
        2: "RF12_Mag",   # AD8302 #1 gain, Con1 vs Con2
        3: "RF12_Ph",    # AD8302 #1 phase, Con1 vs Con2
        4: "RF2_Mag",    # AD8318 on Con2 (forward power)
        5: "RF23_Mag",   # AD8302 #2 gain, Con2 vs Con3
        6: "RF23_Ph",    # AD8302 #2 phase, Con2 vs Con3
        7: "RF3_Mag",    # AD8318 on Con3 (backward power)
        8: "RF4_Mag",    # AD8318 on Con4 (spare; needs ScreenerEnable.RF_SPARE)
    }

    #: PCA9685 channel -> net it drives (channel n is output LED(n-1)).
    PWM_CHANNELS: dict[int, str] = {
        1: "RF_Enable", 2: "RF_Enable2",
        **{2 + n: f"SWEN{n}" for n in range(1, 8)},
        10: "(unconnected)", 11: "FluxgateEnable", 12: "SensorsEnable",
        13: "RTD_Polarity", 14: "MX1", 15: "MX2", 16: "MX3",
    }

    def __init__(self, transport: Transport):
        self._t = transport

    def setup(self) -> ScreenerChips:
        """Probes the board's three chips independently, turning every output
        off and resetting the ADS124S08, and reports which answered (the
        firmware also logs a warning per missing chip). Must be re-run after
        every system enable."""
        fields = self._t.request("setupScreener")
        return ScreenerChips(*(f == "1" for f in fields[:3]))

    # -- outputs ---------------------------------------------------------------

    def set_switch(self, channel: int, duty: float = 1.0) -> None:
        """Sets power switch 1..7: 1.0 fully on, 0 off, in between PWM at
        ~200 Hz. The switches are low-side: a load sits between HP-1 pin 1
        (12 V) and pin 2 (VL)."""
        self._t.request("setScreenerSwitch", channel, float(duty))

    def all_switches_off(self) -> None:
        self._t.request("setScreenerSwitch", 0, 0.0)

    def set_enable(self, which: ScreenerEnable, on: bool = True) -> None:
        """Switches a rail or detector enable (see :class:`ScreenerEnable`)."""
        self._t.request("setScreenerEnable", int(which), on)

    def set_pwm(self, channel: int, duty: float) -> None:
        """Raw PCA9685 output 1..16 (0 = all, off only), for bring-up; see
        :attr:`PWM_CHANNELS` for what each drives."""
        self._t.request("setScreenerPwm", channel, float(duty))

    def set_rf_switch(self, on: bool) -> None:
        """Drives the RF switch control line (5 V TTL on RFS1)."""
        self._t.request("setScreenerRfSwitch", on)

    def all_off(self) -> None:
        """Every PCA9685 output (switches, enables, RTD mux) and the RF
        switch line low. Attempts both even if one fails (e.g. no PCA9685),
        then re-raises the first error."""
        errors: list[SyncBoardError] = []
        for step in (lambda: self.set_pwm(0, 0.0), lambda: self.set_rf_switch(False)):
            try:
                step()
            except SyncBoardError as exc:
                errors.append(exc)
        if errors:
            raise errors[0]

    # -- inputs ----------------------------------------------------------------

    def read_rf(self, channel: int) -> float:
        """RF detector output in volts (ADS7828 channel 1..8, 0..1.8 V); see
        :attr:`RF_CHANNELS`."""
        return float(self._t.request("readScreenerRf", channel)[0])

    def read_adc(self, ainp: int, ainn: int = AINCOM,
                 ref: ScreenerRef = ScreenerRef.INTERNAL, gain: int = 1) -> AdcReading:
        """One ADS124S08 conversion of AIN<ainp> - AIN<ainn> (0..11, or 12 =
        AINCOM) at gain 1..128; gain 1 bypasses the PGA. Takes ~80 ms."""
        fields = self._t.request("readScreenerAdc", ainp, ainn, int(ref), gain)
        return AdcReading(int(fields[0]), float(fields[1]))

    def read_adc_volts(self, ainp: int, ainn: int = AINCOM, gain: int = 1) -> float:
        """:meth:`read_adc` against the internal reference, in volts."""
        return self.read_adc(ainp, ainn, ScreenerRef.INTERNAL, gain).ratio * self.INTERNAL_REF_V

    def read_rtd(self, channel: int, polarity: int = 0) -> float:
        """Four-wire resistance in ohms on RTD channel 1..8 (1-6: HRTD
        headers, 7-8: the sensor flex). Needs ``ScreenerEnable.SENSORS`` on.

        ``polarity`` is the level of the RTD_Polarity line: 0 sends the
        excitation into the D- wire, 1 into D+. Readings are sign-corrected,
        so both should agree; their mean cancels thermal-EMF offsets. Takes
        ~90 ms. Convert with :func:`pt1000_celsius`.
        """
        return float(self._t.request("readScreenerRtd", channel, bool(polarity))[0])

    def read_supply(self, analog: bool = True) -> float:
        """The ADS124S08's supply monitor in volts: AVDD (the board's
        heartbeat-gated 5 V analog rail) or, with ``analog=False``, DVDD
        (3.3 V)."""
        return float(self._t.request("readScreenerSupply", 0 if analog else 1)[0])

    # Flex sensors, read as the "ADC Setup" note on the ScreenerMobo sheet
    # describes. The conditioned signals sit at (sensor + 2V5_Offset) / 2 and
    # are read against 2V5_Offset (AIN5).

    def read_fluxgate(self) -> float:
        """Fluxgate output P - N in volts (AIN2 - AIN3, about +-1.65 V span)."""
        return self.read_adc_volts(2, 3)

    def read_hall(self) -> float:
        """Hall sensor output as a fraction of its excitation:
        (HallS - 2.5 V) / (HallP - HallN), read ratiometrically."""
        return self.read_adc(4, 5, ScreenerRef.HALL).ratio

    def read_humidity(self) -> float:
        """The humidity sensor's RH output in volts, at the sensor."""
        return 2.0 * self.read_adc_volts(9, 5) + self.OFFSET_V

    def read_humidity_temperature(self) -> float:
        """The humidity sensor's temperature output (T_RH) in volts, at the
        sensor."""
        return 2.0 * self.read_adc_volts(8, 5) + self.OFFSET_V


def pt1000_celsius(ohms: float, r0: float = 1000.0) -> float:
    """Converts a platinum RTD resistance to degrees C (IEC 60751, without
    the sub-zero C term: exact from 0 C up, within 0.01 C down to -40 C).
    The screener board's RTD inputs are sized for PT1000s."""
    a, b = 3.9083e-3, -5.775e-7
    return (-a + math.sqrt(a * a - 4 * b * (1 - ohms / r0))) / (2 * b)


class SignalApi:
    """Timed signal sequences (see src/Signals.h for the execution model)."""

    NUM_SIGNALS = 5
    MAX_LENGTH = 2000
    RECORDING_MODES = frozenset(
        {SignalMode.ADC, SignalMode.MAG_ADC, SignalMode.MAG_HALL_READ}
    )

    def __init__(self, transport: Transport):
        self._t = transport

    def record(self, mode: SignalMode, channel: int, n_samples: int,
               interval_ms: float, index: int = 4) -> list[float]:
        """Records ``n_samples`` at a fixed interval and returns them.

        One-call convenience over configure + load_uniform + start + read for
        the recording modes (ADC, MAG_ADC, MAG_HALL_READ); ``channel`` is the
        ADC channel or Hall sensor id. Blocks for the recording duration.
        Uses signal slot ``index`` (default: the last one, to stay clear of
        manually managed slots).
        """
        if mode not in self.RECORDING_MODES:
            raise ValueError(f"{mode!r} is not a recording mode")
        self.configure(index, mode, option=channel)
        self.load_uniform(index, n_samples, interval_ms)
        self.start(index)
        # The firmware stops the signal by itself after the last sample.
        duration_s = n_samples * interval_ms / 1000.0
        self.wait(index, timeout=duration_s * 1.5 + 2.0)
        return self.read(index)

    def is_active(self, index: int) -> bool:
        """True while a signal is running (a repeating signal stays active
        until stopped)."""
        return bool(int(self._t.request("activity")[0]) >> index & 1)

    def wait(self, index: int, timeout: float | None = None,
             poll_interval_s: float = 0.01) -> None:
        """Blocks until the signal finishes.

        Only meaningful for non-repeating signals — a repeating one never
        finishes on its own. Raises TimeoutError if the deadline passes.
        """
        deadline = None if timeout is None else time.monotonic() + timeout
        while self.is_active(index):
            if deadline is not None and time.monotonic() >= deadline:
                raise TimeoutError(f"signal {index} still active after {timeout:.1f} s")
            time.sleep(poll_interval_s)

    def configure(self, index: int, mode: SignalMode, option: int = 0,
                  repeat: bool = False, is_slave: bool = False) -> None:
        """Sets a signal's mode and target.

        ``option`` selects the target channel/pin/sensor for most modes; for
        CONDUCTOR it is a bitmask of the slave signal indices to drive.
        """
        self._t.request("setupSignal", index, int(mode), option, repeat, is_slave)

    def load(self, index: int, values: Sequence[float],
             delays_ms: Sequence[float] | None = None) -> None:
        """Loads the step table: at each step the value is applied, then the
        corresponding delay elapses. For slave signals delays are ignored
        (the conductor provides the timebase) and may be omitted.
        """
        if delays_ms is None:
            delays_ms = [0.0] * len(values)
        if len(values) != len(delays_ms):
            raise ValueError("values and delays_ms must have the same length")
        interleaved: list[float] = []
        for value, delay in zip(values, delays_ms):
            interleaved += [float(value), float(delay)]
        self._t.request("loadSignal", index, len(values), *interleaved, timeout=10.0)

    def load_uniform(self, index: int, count: int, interval_ms: float) -> None:
        """Loads ``count`` steps at a fixed interval (typical for recording)."""
        self._t.request("loadSignalUniform", index, count, float(interval_ms))

    def start(self, index: int) -> None:
        """Starts a signal from step 0 (slaves of a conductor start with it)."""
        self._t.request("startSignal", index)

    def stop(self, index: int) -> None:
        self._t.request("stopSignal", index)

    def read(self, index: int) -> list[float]:
        """Returns a signal's data buffer (recorded samples for ADC-type
        modes).

        Reading while signals run works, but the transfer briefly stalls the
        firmware's signal engine, leaving a small timing gap; prefer reading
        after the signal has stopped.
        """
        fields = self._t.request("readSignal", index, timeout=10.0)
        count = int(fields[0])
        return [float(f) for f in fields[1 : 1 + count]]


class IoApi:
    def __init__(self, transport: Transport):
        self._t = transport

    def read_di(self, channel: int) -> bool:
        """Reads a fixed digital input (1..4)."""
        return self._t.request("readDi", channel)[0] == "1"

    def write_do(self, channel: int, high: bool) -> None:
        """Sets a fixed digital output (1..4)."""
        self._t.request("writeDo", channel, high)

    def pulse_do(self, channel: int, duration_ms: float) -> None:
        """Pulses a fixed digital output high for ``duration_ms``."""
        self._t.request("pulseDo", channel, float(duration_ms))

    def setup_gpio(self, gpio: int, direction: str, enabled: bool = True) -> None:
        """Configures a GPIO (label 13, 25..32); applied on the next enable.

        ``direction`` is "input" or "output".
        """
        if direction not in ("input", "output"):
            raise ValueError("direction must be 'input' or 'output'")
        self._t.request("setupGpio", gpio, enabled, direction == "input")

    def write_gpio(self, gpio: int, high: bool) -> None:
        self._t.request("writeGpio", gpio, high)

    def read_gpio(self, gpio: int) -> bool:
        return self._t.request("readGpio", gpio)[0] == "1"

    def read_adc(self, channel: int) -> float:
        """Reads a SyncBoard ADC channel (1..8) in volts."""
        return float(self._t.request("readAdc", channel)[0])

    def set_dac(self, channel: int, volts: float) -> None:
        """Sets a SyncBoard DAC channel (1..8, or 0 for all) to 0..3.3 V."""
        self._t.request("setDac", channel, float(volts))

    def set_switch(self, channel: int, duty: float) -> None:
        """Sets a 12 V power switch (1..16; 0 switches all off). ``duty`` of
        1.0 is fully on; intermediate values PWM at ~200 Hz."""
        self._t.request("setSwitch", channel, float(duty))


class ImagingApi:
    MAX_FRAMES = 4

    def __init__(self, transport: Transport):
        self._t = transport
        self._num_frames = 0
        self._led_by_camera = False

    def set_sync_mode(self, mode: int, led_by_camera: bool = False) -> None:
        """mode: 0 = triggers ignored, 1 = host-started sequences,
        2 = externally triggered sequences. ``led_by_camera`` hands per-frame
        LED timing to the camera's gating outputs."""
        self._t.request("setSyncMode", mode, led_by_camera)
        self._led_by_camera = led_by_camera

    def configure(self, frames: Sequence[Frame]) -> None:
        """Configures the sequence as a list of 1..4 frames."""
        if not 1 <= len(frames) <= self.MAX_FRAMES:
            raise ValueError(f"need 1..{self.MAX_FRAMES} frames")
        if not self._led_by_camera and any(f.exposure_ms <= 0 for f in frames):
            raise ValueError(
                "every frame needs a positive exposure_ms unless the sync mode "
                "was set with led_by_camera=True"
            )
        args: list = []
        for i in range(self.MAX_FRAMES):
            frame = frames[i] if i < len(frames) else None
            args += [frame is not None,
                     frame.led if frame else 0,
                     float(frame.exposure_ms) if frame else 0.0]
        self._t.request("setupImaging", *args)
        self._num_frames = len(frames)

    def start(self) -> None:
        """Starts the configured sequence (sync mode 1)."""
        if self._num_frames == 0:
            raise RuntimeError("no sequence configured; call configure() first")
        self._t.request("startImaging", self._num_frames)

    def is_running(self) -> bool:
        """True while an image sequence is in flight."""
        return self._t.request("activity")[1] == "1"

    def read_camera_inputs(self) -> CameraInputs:
        """Reads the camera interface lines directly — for bring-up checks
        and debugging camera integration ('is the camera asserting ready?')."""
        fields = self._t.request("readCameraInputs")
        return CameraInputs(
            trigger_ready=fields[0] == "1",
            reading=fields[1] == "1",
            led=tuple(f == "1" for f in fields[2:6]),  # type: ignore[arg-type]
        )

    def wait(self, timeout: float | None = None,
             poll_interval_s: float = 0.01) -> None:
        """Blocks until the current image sequence finishes.

        Raises TimeoutError if the deadline passes (note the firmware itself
        aborts and disables after its 10 s sequence watchdog).
        """
        deadline = None if timeout is None else time.monotonic() + timeout
        while self.is_running():
            if deadline is not None and time.monotonic() >= deadline:
                raise TimeoutError(f"image sequence still running after {timeout:.1f} s")
            time.sleep(poll_interval_s)
