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
from dataclasses import dataclass
from enum import IntEnum
from typing import Sequence

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


@dataclass(frozen=True)
class Status:
    enabled: bool
    led_board_attached: bool
    magnet_board_attached: bool
    sync_mode: int


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
class Frame:
    """One frame of an image sequence."""

    led: int = 0             # LED channel lit during the frame; 0 = none
    exposure_ms: float = 0.0  # 0 = ended by the camera's LED gating signal


class SyncBoard:
    """Owns the transport and exposes the board through per-subsystem APIs:
    :attr:`leds`, :attr:`magnet`, :attr:`signals`, :attr:`io`, :attr:`imaging`.
    """

    ENABLE_TIMEOUT_S = 5.0  # disable blocks ~1 s in firmware

    def __init__(self, transport: Transport):
        self._t = transport
        self.leds = LedApi(transport)
        self.magnet = MagnetApi(transport)
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
        )

    def initialise(self, led_board: bool = False, magnet_board: bool = False) -> None:
        """Convenience bring-up: declare attached boards, enable the system,
        and set up the magnet board if present. Safe to call regardless of
        the board's current state."""
        self.disable()  # board attachment requires a disabled system
        self.attach_led_board(led_board)
        self.attach_magnet_board(magnet_board)
        self.enable()
        if magnet_board:
            self.magnet.setup()

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

    def scan_i2c(self) -> list[int]:
        """Returns the responding I2C addresses (requires an enabled system)."""
        fields = self._t.request("scanI2c", timeout=10.0)
        return [int(f) for f in fields[1:]]


class LedApi:
    CALIBRATE_TIMEOUT_S = 60.0

    def __init__(self, transport: Transport):
        self._t = transport

    def calibrate(self, channel: int, max_current_a: float) -> None:
        """Sweeps the LED to map levels 0..1 onto [turn-on, max_current_a].

        Run once per newly connected LED; the result is stored in EEPROM.
        Blocks for a few seconds and flashes the LED.
        """
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


class SignalApi:
    """Timed signal sequences (see src/Signals.h for the execution model)."""

    NUM_SIGNALS = 5
    MAX_LENGTH = 2000

    def __init__(self, transport: Transport):
        self._t = transport

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
        modes). Safe to call while the signal is running."""
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

    def set_sync_mode(self, mode: int, led_by_camera: bool = False) -> None:
        """mode: 0 = triggers ignored, 1 = host-started sequences,
        2 = externally triggered sequences. ``led_by_camera`` hands per-frame
        LED timing to the camera's gating outputs."""
        self._t.request("setSyncMode", mode, led_by_camera)

    def configure(self, frames: Sequence[Frame]) -> None:
        """Configures the sequence as a list of 1..4 frames."""
        if not 1 <= len(frames) <= self.MAX_FRAMES:
            raise ValueError(f"need 1..{self.MAX_FRAMES} frames")
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
