import logging
import re
import time
import threading
from typing import Any, Dict, List, Optional, Tuple, Union

from syncboard.serialconnection import SerialConnection
from syncboard.command import Command


LOGGER = logging.getLogger(__name__)


class SyncBoardController:
    LED_ID = [1, 2, 3, 4, 7]
    LED_MAX_CURRENT = {1: 10.0, 2: 8.0, 3: 12.0, 4: 8.0, 7: 6.0}
    DEFAULT_POLL_INTERVAL_S = 0.01

    def __init__(
        self,
        serial_connection: SerialConnection,
        poll_interval_s: float = DEFAULT_POLL_INTERVAL_S,
    ):
        self.connection = serial_connection
        self.poll_interval_s = poll_interval_s
        self._lock = threading.RLock()

        self._is_initialised: bool = False
        "Set to true after initialise(). Set to false after finalise()."

        # LED configurations
        self._led_configs: Dict[int, Dict[str, Union[int, float, None, str]]] = {
            led_id: {'mode': None, 'intensity': None, 'status': None, 'stop_time': None} for led_id in self.LED_ID
        }

    @classmethod
    def from_serial_port(
        cls, port: str = '/dev/ttyAMC0', baud_rate: int = 2000000, *tiger_args, **tiger_kwargs
    ) -> "SyncBoardController":
        return cls(SerialConnection(port, baud_rate), *tiger_args, **tiger_kwargs)

    def attach_leds(self):
        self.send_command(Command.format(Command.ATTACH_LED, True))

    def calibrate_led(
            self,
            led_id: int,
            max_current: Optional[float] = None,
    ):
        if led_id not in self.LED_ID:
            raise ValueError(f"LED ID {led_id} not available. Must be in {self.LED_ID}.")
        max_current = self.LED_MAX_CURRENT[led_id] if max_current is None else max_current
        LOGGER.info(f"Calibrating LED {led_id} with max current {max_current}.")
        self.send_command(Command.format(Command.CALIBRATE_LED, led_id, max_current))

    def disable_system(self):
        self.send_command(Command.format(Command.SYSTEM_DISABLE))

    def disable_led(
            self,
            led_id: int,
    ):
        if led_id not in self.LED_ID:
            LOGGER.warning(f"LED ID {led_id} unknown. Disabling all LEDs.")
            led_id = -1
        if led_id == -1:
            led_ids = self.LED_ID
        else:
            led_ids = [led_id]
        for _led_id in led_ids:
            self.send_command(Command.format(Command.SWITCH_LED, _led_id, 0))
            self._led_configs[_led_id]['status'] = "on"

    def enable_led(
            self,
            led_id: int,
            intensity: float = 0.1,
            duration: Optional[float] = None,
    ):
        """

        Parameters
        ----------
        led_id: int                 LED ID
        intensity: float            LED brightness in [0,1]
        duration: Optional[float]   If not None, switches LED on for duration only (in milliseconds).

        Returns
        -------

        """
        if led_id not in self.LED_ID:
            raise ValueError(f"LED ID {led_id} not available. Must be in {self.LED_ID}.")
        self.setup_led(led_id=led_id, intensity=intensity)
        if duration is None:
            self.send_command(Command.format(Command.SWITCH_LED, led_id, 1))
            self._led_configs[led_id]['status'] = "on"
        else:
            self.send_command(Command.format(Command.SWITCH_LED_TIMED, led_id, duration))
            self._led_configs[led_id]['status'] = "timed"
            self._led_configs[led_id]['stop_time'] = time.time() + duration * 1000.0

    def enable_system(self):
        self.send_command(Command.format(Command.SYSTEM_ENABLE))

    def factory_reset(self):
        self.send_command(Command.format(Command.FACTORY_RESET))

    def finalise(self):
        self.disable_system()
        self._is_initialised = False

    def initialise(self):
        self.attach_leds()
        self.enable_system()
        self._is_initialised = True

    def led_is_on(self, led_id: int):
        if led_id not in self.LED_ID:
            raise ValueError(f"LED ID {led_id} not available. Must be in {self.LED_ID}.")
        return self._led_configs[led_id]['status'] == 'on' or (self._led_configs[led_id]['status'] == 'timed' and
                                                               time.time() > self._led_configs[led_id]['stop_time'])

    def send_command(self, command: str) -> str:
        LOGGER.debug(f"Sending command {command}.")
        with self._lock:
            self.connection.send_command(command)
            # TODO SyncBoard does not seem to respond that fast
            response = self.connection.read_response()

        return response

    def set_dac(self, channel: int, voltage: float):
        self.send_command(Command.format(Command.SET_DAC, channel, voltage))

    def setup_gpio(self, gpio_num: int, enable: int, mode: int, output_state: int):
        self.send_command(Command.format(Command.SETUP_GPIO, gpio_num, enable, mode, output_state))

    def setup_led(
            self,
            led_id: int,
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
        if led_id not in self.LED_ID:
            raise ValueError(f"LED ID {led_id} not available. Must be in {self.LED_ID}.")
        if self._is_equal_led_config(led_id=led_id, feedback_mode=feedback_mode, intensity=intensity):
            LOGGER.debug(f"LED {led_id} already configured: {self._led_configs[led_id]}")
            return
        self.send_command(Command.format(Command.SETUP_LED, led_id, feedback_mode, intensity))
        self._led_configs[led_id]['mode'] = feedback_mode
        self._led_configs[led_id]['intensity'] = intensity

    def setup_signal_dac(self):
        # TODO
        return

    def setup_signal_mode(self, index: int, repeat: int, dac: int, channel: int):
        self.send_command(Command.format(Command.SETUP_SIGNAL_MODE, index, repeat, dac, channel))

    def start_signal(self, index: int):
        self.send_command(Command.format(Command.START_SIGNAL, index))

    def stop_signal(self, index: int):
        self.send_command(Command.format(Command.STOP_SIGNAL, index))

    def _is_equal_led_config(
            self,
            led_id: int,
            feedback_mode: int = 0,
            intensity: float = 0.1,
    ) -> bool:
        return self._led_configs[led_id]['mode'] == feedback_mode and \
            self._led_configs[led_id]['intensity'] == intensity

    def _led_is_setup(self, led_id: int):
        return self._led_configs[led_id]['mode'] is not None and self._led_configs[led_id]['intensity'] is not None
