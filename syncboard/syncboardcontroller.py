from __future__ import annotations

from enum import Enum
import logging
import re
import serial.tools.list_ports
from termios import error as TermiosError
import time
import threading
from typing import Any, Dict, List, Optional, Tuple, Union

from syncboard.serialconnection import SerialConnection, SerialProtocolError, SerialResponseTimeout
from syncboard.command import Command


LOGGING_LEVEL = logging.INFO
FORMATTER = logging.Formatter('%(asctime)s - %(levelname)s - %(name)s - %(message)s')
LOGGER = logging.getLogger(__name__)
for handler in LOGGER.handlers:
    LOGGER.removeHandler(handler)
LOGGER.setLevel(LOGGING_LEVEL)
handler = logging.StreamHandler()
handler.setFormatter(FORMATTER)
LOGGER.addHandler(handler)
LOGGER.propagate = False

# regex search string that can handle scientific notation, e.g. 1.2 -> 1.2, 1.2e-1 -> 0.12, 1.2e2 -> 120
float_rgx = re.compile(r'[-+]?[0-9]*\.?[0-9]+([eE][-+]?[0-9]+)?')


class SignalMode(Enum):
    ADC = 0
    DAC = 1
    GPIO = 2
    MAGDAC = 3
    MAGADC = 4
    MAGHALL_READ = 5  # read the hall sensor in mT
    MAGCURR_WRITE = 6 # set the magnet current calibrated to 0
    MAGHALL_WRITE = 7 # set the magnet current in mT based on Hall calibration
    DO = 8
    CONDUCTOR = 9
    LED = 10
    LED_TIMED = 11
    DO_TIMED = 12
    GPIO_WRITE = 13


class LED_ID(int, Enum):
    # LEDType.LED_385_NM: 7,
    # LEDType.LED_450_NM: 1,
    # LEDType.LED_515_NM: 2,
    # LEDType.LED_565_NM: 3,
    # LEDType.LED_645_NM: 4,
    # LEDType.NO_LED: None,
    # LED_385_NM = 5 not sure which one this is
    LED_450_NM = 5
    LED_515_NM = 2
    LED_565_NM = 3
    LED_645_NM = 4
    LED_385_NM = 7
    LED_DPC = 8
    NO_LED = -1


class SyncBoardController:
    # LED_ID = [1, 2, 3, 4, 7]
    LED_MAX_CURRENT = {1: 10.0, 2: 8.0, 3: 12.0, 4: 8.0, 7: 6.0}
    DEFAULT_POLL_INTERVAL_S = 0.01
    DEFAULT_RESPONSE_TIMEOUT_S = 1.0
    SYSTEM_STATE_TIMEOUT_S = 2.0
    LED_CALIBRATION_TIMEOUT_S = 60.0
    MAX_RECONNECT_ATTEMPTS = 100

    def __init__(
        self,
        serial_connection: SerialConnection,
        port: str,
        baud_rate: int,
        poll_interval_s: float = DEFAULT_POLL_INTERVAL_S,
    ):
        self.connection = serial_connection  # You should never directly use this
        self.port: str = port
        self.baud_rate: int = baud_rate
        self.poll_interval_s = poll_interval_s
        self._lock = threading.RLock()

        self._is_initialised: bool = False
        "Set to true after initialise(). Set to false after finalise()."
        self._num_reconnect_attempts: int = 0
        "Resets after a successful reconnect; guards against repeated failures."
        # LED configurations
        self._led_configs: Dict[LED_ID, Dict[str, Union[int, float, None, str]]] = {
            led_id: {'mode': None, 'intensity': None, 'status': None, 'stop_time': None} for led_id in LED_ID.__members__.values()
        }

    @classmethod
    def from_serial_port(
        cls, port: str | None = '/dev/ttyACM1', baud_rate: int = 2000000, *syncboard_args, **syncboard_kwargs
    ) -> "SyncBoardController":
        if port is None:
            port = cls.get_syncboard_port()
        return cls(SerialConnection(port, baud_rate), port, baud_rate, *syncboard_args, **syncboard_kwargs)

    @staticmethod
    def get_syncboard_port(hwid: str = "16C0:0483") -> str:
        def get_pid(in_str):
            match = re.search(r'VID:PID=(\w+:\w+)', in_str)
            if match:
                return match.group(1)
            else:
                return None
        ports = list(serial.tools.list_ports.comports())
        for port in ports:
            if get_pid(port.hwid) == hwid:
                return port.device
        raise RuntimeError("Syncboard not found")

    def attach_leds(self):
        self.send_command(Command.format(Command.ATTACH_LED, True))
        
    def attach_magnet(self):
        self.send_command(Command.format(Command.ATTACH_MAGNET, True))

    def enable_magnet(self, enable: bool = True):
        self.send_command(
            Command.format(Command.ENABLE_MAGNET, enable),
            expected_response="Enabled magnet ",
        )
    
    def set_magnet_current(self, current: float, NC: bool = True):
        self.send_command(
            Command.format(Command.SET_MAGNET_CURRENT, int(NC), current),
            expected_response="Set magnet current",
        )
        
    def set_magnet_field(self, field: float, NC: bool = True):
        self.send_command(
            Command.format(Command.SET_MAGNET_FIELD, int(NC), field),
            expected_response="Set magnetic field",
        )
    
    def read_hall(self, hall_id: int) -> float:
        response = self.send_command(
            Command.format(Command.READ_HALL, hall_id),
            wait_time=0.1,
            expected_response="Read value ",
        )
        # print(response)
        return float(float_rgx.search(response).group())
    
    def calibrate_led(
            self,
            led_id: LED_ID,
            max_current: Optional[float] = None,
    ):
        # if led_id not in self.LED_ID:
        #     raise ValueError(f"LED ID {led_id} not available. Must be in {self.LED_ID}.")
        max_current = self.LED_MAX_CURRENT[led_id] if max_current is None else max_current
        LOGGER.info(f"Calibrating LED {led_id} with max current {max_current}.")
        self.send_command(
            Command.format(Command.CALIBRATE_LED, led_id.value, max_current),
            wait_time=self.LED_CALIBRATION_TIMEOUT_S,
        )

    def calibrate_magnet(self):
        response = self.send_command(
            Command.format(Command.CALIBRATE_MAGNET),
            wait_time=5,
            expected_response="Calibrated magnet",
        )
        # print(response)

    def calibrate_hall(self, hall_id: int):
        response = self.send_command(
            Command.format(Command.CALIBRATE_HALL, hall_id),
            wait_time=2,
            expected_response=f"Calibrated Hall sensor {hall_id}",
        )
        # print(response)

    def disable_system(self):
        self.send_and_wait_for_text(
            Command.format(Command.SYSTEM_DISABLE),
            "System disabled",
            response_timeout_s=self.SYSTEM_STATE_TIMEOUT_S,
        )

    def disable_led(
            self,
            led_id: Optional[LED_ID] = None,
            fast=False,
    ):
        # if (led_id is not None) and (led_id not in self.LED_ID):
        #     LOGGER.warning(f"LED ID {led_id} unknown. Disabling all LEDs.")
        #     led_id = None
        send_command = self.fast_send_command if fast else self.send_command
        if led_id is None:
            self.disable_all_leds()
        else:
            send_command(Command.format(Command.SWITCH_LED, led_id.value, 0))
            self._led_configs[led_id]['status'] = "off"

    def disable_all_leds(self):
        self.send_command(Command.format(Command.DISABLE_ALL_LEDS))
        for _led_id in LED_ID.__members__.values():
            self._led_configs[_led_id]['status'] = "off"

    def enable_led(
            self,
            led_id: LED_ID,
            intensity: float = 0.1,
            duration: Optional[float] = None,
            fast=False,
    ):
        """

        Parameters
        ----------
        led_id: int                 LED ID
        intensity: float            LED brightness in [0,1] (and NOT in [0,100])
        duration: Optional[float]   If not None, switches LED on for duration only (in milliseconds).

        Returns
        -------

        """
        send_command = self.fast_send_command if fast else self.send_command
        # if led_id not in self.LED_ID:
        #     raise ValueError(f"LED ID {led_id} not available. Must be in {self.LED_ID}.")
        if intensity > 0.29 or intensity < 0:
            LOGGER.warning(f"Received intensity {intensity} for enable_led. Duration is {duration}.")
        self.setup_led(led_id=led_id, intensity=intensity)
        if (duration is None) and (intensity <= 0.29):
            send_command(Command.format(Command.SWITCH_LED, led_id.value, 1))
            self._led_configs[led_id]['status'] = "on"
        else:
            if duration is None:
                duration = 3000
                LOGGER.warning(f"Setting LED for {duration} miliseconds.")
            
            send_command(Command.format(Command.SWITCH_LED_TIMED, led_id.value, duration))
            
            self._led_configs[led_id]['status'] = "timed"
            self._led_configs[led_id]['stop_time'] = time.time() + duration / 1000.0

    def enable_magnet(self, enable: bool = True):
        self.send_command(
            Command.format(Command.ENABLE_MAGNET, enable),
            expected_response="Enabled magnet ",
        )

    def enable_system(self):
        self.send_and_wait_for_text(
            Command.format(Command.SYSTEM_ENABLE),
            "System enabled",
            response_timeout_s=self.SYSTEM_STATE_TIMEOUT_S,
        )

    def factory_reset(self):
        self.send_without_response(Command.format(Command.FACTORY_RESET))

    def finalise(self):
        self.disable_system()
        self._is_initialised = False

    def initialise(self, force_init: bool = False):
        if not force_init:
            if self.is_initialised():
                LOGGER.warning("Sync board already initialised. Returning.")
                return
        self.attach_leds()
        self.attach_magnet()
        self.enable_system()
        self.setup_magnet()
        self._setup_leds()
        self._is_initialised = True

    def is_initialised(self) -> bool:
        return self._is_initialised

    def led_is_on(self, led_id: int):
        # if led_id not in self.LED_ID:
        #     raise ValueError(f"LED ID {led_id} not available. Must be in {self.LED_ID}.")
        return self._led_configs[led_id]['status'] == 'on' or (self._led_configs[led_id]['status'] == 'timed' and
                                                               time.time() > self._led_configs[led_id]['stop_time'])

    def read_hall(self, hall_id: int) -> float:
        response = self.send_command(
            Command.format(Command.READ_HALL, hall_id),
            wait_time=0.1,
            expected_response="Read value ",
        )
        # print(response)
        return float(float_rgx.search(response).group())

    def read_photodiode(self, channel: int = 8) -> float | None:
        response_str = self.send_command(Command.format(Command.MEASURE_PHOTODIODE, channel), wait_time=1)
        try:
            return float(response_str.rstrip('#%').lstrip('$').split('/')[1])
        except Exception as e:
            LOGGER.warning(f"Received malformatted response from read_photodiode: {response_str}, {e}")
            return None

    def reconnect(self):
        self._num_reconnect_attempts += 1
        if self._num_reconnect_attempts > self.MAX_RECONNECT_ATTEMPTS:
            msg = f"SyncBoardController.reconnect: max reconnect attempts " \
                  f"({self._num_reconnect_attempts}) reached."
            LOGGER.error(msg)
            raise RuntimeError(msg)
        # Try closing the previous connection
        try:
            self.connection.disconnect()
        except Exception:
            try:
                self.connection.connection.close()
            except Exception:
                pass
        time.sleep(1)
        # Try reconnecting on different port
        old_port = self.port
        self.port = self.get_syncboard_port()
        msg = f"SyncBoardController.reconnect: reconnecting at attempt {self._num_reconnect_attempts} " \
              f"on new port {self.port} after error on old port {old_port}."
        LOGGER.warning(msg)
        try:
            self.connection = SerialConnection(self.port, self.baud_rate)
        except Exception as E:
            msg = f"SyncBoardController.reconnect: error during reconnection: {E}."
            LOGGER.error(msg)
            raise RuntimeError(msg)
        msg = f"SyncBoardController.reconnect: successfully reconnected on port {self.port}."
        LOGGER.warning(msg)
        self._num_reconnect_attempts = 0

    def fast_send_command(self, command: str) -> None:
        """Compatibility alias for a normal framed command.

        ``switchLED`` replies, so skipping its reply would desynchronise the
        next command.  Framed reads make this fast without sacrificing that
        invariant.
        """
        self.send_command(command)

    def send_without_response(self, command: str) -> None:
        """Send a firmware command that is documented not to reply."""
        with self._lock:
            try:
                self.connection.send(command.encode("ascii"))
            except (TermiosError, serial.serialutil.SerialException, serial.SerialException, OSError) as exc:
                self._recover_transport("send_without_response", exc)
                raise

    def send_and_wait_for_text(
        self,
        command: str,
        expected_text: str,
        response_timeout_s: float,
    ) -> str:
        """Use a legacy textual completion message as a command barrier."""
        with self._lock:
            try:
                return self.connection.send_and_wait_for_text(
                    command,
                    expected_text=expected_text,
                    response_timeout_s=response_timeout_s,
                )
            except (TermiosError, SerialProtocolError, serial.serialutil.SerialException, serial.SerialException, OSError) as exc:
                self._recover_transport("send_and_wait_for_text", exc)
                raise

    def send_command(
        self,
        command: str,
        wait_time: float = 0,
        expected_response: Optional[str] = None,
    ) -> str:
        """Send a reply-producing command and wait only for its framed reply.

        ``wait_time`` is retained for API compatibility and is now a total
        response deadline.  It is no longer an unconditional post-reply wait.
        """
        response_timeout_s = wait_time if wait_time > 0 else self.DEFAULT_RESPONSE_TIMEOUT_S
        expected_response = expected_response or self._command_name(command)
        with self._lock:
            try:
                response = self.connection.send_command(
                    command,
                    response_timeout_s=response_timeout_s,
                    expected_response=expected_response,
                )
            except SerialResponseTimeout as exc:
                # The command may have been executed.  Never replay it: this
                # protocol has no request ID to make a retry unambiguous.
                self._recover_transport("send_command", exc)
                raise
            except (TermiosError, SerialProtocolError, serial.serialutil.SerialException, serial.SerialException, OSError) as exc:
                self._recover_transport("send_command", exc)
                raise

        if self._command_name(response) == "error":
            LOGGER.error(response.rstrip('#%').removeprefix('$error/'))
        return response

    @staticmethod
    def _command_name(frame: str) -> str:
        payload = frame.lstrip('$').rstrip('#%')
        return payload.split('/', 1)[0]

    def _recover_transport(self, operation: str, exc: Exception) -> None:
        LOGGER.error("SyncBoardController.%s: received error: %s", operation, exc)
        try:
            self.reconnect()
        except Exception as reconnect_exc:
            LOGGER.error("SyncBoardController.%s: reconnect failed: %s", operation, reconnect_exc)

    def set_dac(self, channel: int, voltage: float):
        self.send_command(Command.format(Command.SET_DAC, channel, voltage))

    def set_magnet_current(self, current: float, NC: bool = True):
        self.send_command(
            Command.format(Command.SET_MAGNET_CURRENT, int(NC), current),
            expected_response="Set magnet current",
        )

    def set_magnet_field(self, field: float, NC: bool = True):
        self.send_command(
            Command.format(Command.SET_MAGNET_FIELD, int(NC), field),
            expected_response="Set magnetic field",
        )

    def setup_gpio(self, gpio_num: int, enable: int, mode: int, output_state: int):
        # The current firmware prints a diagnostic but does not send a framed
        # response for setupGPIO, so this must remain a one-way command.
        self.send_without_response(Command.format(Command.SETUP_GPIO, gpio_num, enable, mode, output_state))

    def setup_led(
            self,
            led_id: LED_ID,
            feedback_mode: int = 0,
            intensity: float = 0.1,
    ):
        """

        Parameters
        ----------
        led_id: int         LED ID 0,1,2,...
        feedback_mode: int  0 for current feedback mode, 1 for optical feedback mode
        intensity: float    LED intensity in [0,1]

        Returns
        -------

        """
        if led_id == LED_ID.NO_LED:
            LOGGER.debug(f"Nothing to setup for led_id {led_id}.")
            return
        # if led_id not in self.LED_ID:
        #     raise ValueError(f"LED ID {led_id} not available. Must be in {self.LED_ID}.")
        if self._is_equal_led_config(led_id=led_id, feedback_mode=feedback_mode, intensity=intensity):
            LOGGER.debug(f"LED {led_id} already configured: {self._led_configs[led_id]}")
            return
        self.send_command(Command.format(Command.SETUP_LED, led_id.value, feedback_mode, intensity))
        self._led_configs[led_id]['mode'] = feedback_mode
        self._led_configs[led_id]['intensity'] = intensity

    def _setup_leds(self):
        for _led_id in LED_ID.__members__.values():
            self.setup_led(led_id=_led_id)

    def setup_magnet(self):
        self.send_command(Command.format(Command.SETUP_MAGNET), expected_response="Setup magnet board complete")

    def setup_signal_dac(self):
        # TODO
        return

    def write_do(self, channel: int, state: int):
        return self.send_command(Command.format(Command.WRITE_DO, channel, state))

    def write_gpio(self, gpio_num: int, state: int):
        return self.send_command(Command.format(Command.WRITE_GPIO, gpio_num, state))

    def setup_signal_mode(self, index: int, repeat: int, mode: SignalMode, options: int, is_slave: bool = False):
        self.send_command(Command.format(Command.SETUP_SIGNAL_MODE, index, repeat, mode.value, options, is_slave))

    def setup_signal_output(self, index: int, vals, timings):
        '''
        index: int - signal index
        vals: list - list of values to be sent 
            For LED_TIMED, this is a list of positive and negative durations, where positive means on and negative means do nothing
            Make sure that the LED durations are shorter than the sequence durations else will attempt to turn on LED when it is already on
        timings: list - list of timings for each value in ms
        '''
        num_vals = len(vals)
        # make a list of vals, timings interleaved
        vals = [item for sublist in zip(vals, timings) for item in sublist]
        self.send_command(Command.format(Command.SETUP_SIGNAL_OUT, index, num_vals, *vals))

    def setup_signal_conductor(self, index: int, timings: List[float]):
        num_vals = len(timings)
        self.send_command(Command.format(Command.SETUP_SIGNAL_CONDUCTOR, index, num_vals, *timings))

    def start_signal(self, index: int):
        self.send_command(Command.format(Command.START_SIGNAL, index))

    def stop_signal(self, index: int):
        self.send_command(Command.format(Command.STOP_SIGNAL, index))

    def scan_i2c(self):
        self.send_without_response(Command.format(Command.SCAN_I2C))
        return ""

    def _is_equal_led_config(
            self,
            led_id: LED_ID,
            feedback_mode: int = 0,
            intensity: float = 0.1,
    ) -> bool:
        return self._led_configs[led_id]['mode'] == feedback_mode and \
            self._led_configs[led_id]['intensity'] == intensity

    def _led_is_setup(self, led_id: LED_ID):
        return self._led_configs[led_id]['mode'] is not None and self._led_configs[led_id]['intensity'] is not None
