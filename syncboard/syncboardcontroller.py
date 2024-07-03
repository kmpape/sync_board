import logging
import re
import time
import threading
from typing import Any, Dict, List, Optional, Tuple, Union

from syncboard.serialconnection import SerialConnection
from syncboard.command import Command


FORMATTER = logging.Formatter('%(asctime)s - %(levelname)s - %(name)s - %(message)s')
LOGGING_LEVEL = logging.INFO
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
        cls, port: str = '/dev/ttyACM1', baud_rate: int = 2000000, *tiger_args, **tiger_kwargs
    ) -> "SyncBoardController":
        return cls(SerialConnection(port, baud_rate), *tiger_args, **tiger_kwargs)

    def attach_leds(self):
        self.send_command(Command.format(Command.ATTACH_LED, True))
        
    def attach_magnet(self):
        self.send_command(Command.format(Command.ATTACH_MAGNET, True))

    def enable_magnet(self, enable: bool = True):
        self.send_command(Command.format(Command.ENABLE_MAGNET, enable))
    
    def set_magnet_current(self, current: float, NC: bool = True):
        self.send_command(Command.format(Command.SET_MAGNET_CURRENT, int(NC), current))
        
    def set_magnet_field(self, field: float, NC: bool = True):
        self.send_command(Command.format(Command.SET_MAGNET_FIELD, int(NC), field))
    
    def read_hall(self, hall_id: int) -> float:
        response = self.send_command(Command.format(Command.READ_HALL, hall_id), wait_time=0.1)
        print(response)
        return float(float_rgx.search(response).group())
    
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

    def calibrate_magnet(self):
        response = self.send_command(Command.format(Command.CALIBRATE_MAGNET), wait_time=5)
        print(response)

    def calibrate_hall(self, hall_id: int):
        response = self.send_command(Command.format(Command.CALIBRATE_HALL, hall_id), wait_time=2)
        print(response)

    def disable_system(self):
        self.send_command(Command.format(Command.SYSTEM_DISABLE))

    def disable_led(
            self,
            led_id: Optional[int] = None,
    ):
        if (led_id is not None) and (led_id not in self.LED_ID):
            LOGGER.warning(f"LED ID {led_id} unknown. Disabling all LEDs.")
            led_id = None
        led_ids = self.LED_ID if led_id is None else [led_id]
        for _led_id in led_ids:
            self.send_command(Command.format(Command.SWITCH_LED, _led_id, 0))
            self._led_configs[_led_id]['status'] = "off"

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
        intensity: float            LED brightness in [0,1] (and NOT in [0,100])
        duration: Optional[float]   If not None, switches LED on for duration only (in milliseconds).

        Returns
        -------

        """
        if led_id not in self.LED_ID:
            raise ValueError(f"LED ID {led_id} not available. Must be in {self.LED_ID}.")
        if intensity > 0.29 or intensity < 0:
            LOGGER.warning(f"Received intensity {intensity} for enable_led.")
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
        if led_id not in self.LED_ID:
            raise ValueError(f"LED ID {led_id} not available. Must be in {self.LED_ID}.")
        return self._led_configs[led_id]['status'] == 'on' or (self._led_configs[led_id]['status'] == 'timed' and
                                                               time.time() > self._led_configs[led_id]['stop_time'])

    def read_photodiode(self, channel: int = 8) -> float | None:
        response_str = self.send_command(Command.format(Command.MEASURE_PHOTODIODE, channel), wait_time=1)
        try:
            return float(response_str.rstrip('#%').lstrip('$').split('/')[1])
        except Exception as e:
            LOGGER.warning(f"Received malformatted response from read_photodiode: {response_str}, {e}")
            return None

    def send_command(self, command: str, wait_time: float = 0) -> str:
        LOGGER.debug(f"Sending command {command}.")
        with self._lock:
            self.connection.send_command(command)
            # TODO SyncBoard does not seem to respond that fast
            response = self.connection.read_response(wait_time=wait_time)
        if 'error' in response:
            LOGGER.error(response.rstrip('#%').lstrip('$error/'))
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
        if led_id is None:
            LOGGER.debug(f"Nothing to setup for led_id {led_id}.")
            return
        if led_id not in self.LED_ID:
            raise ValueError(f"LED ID {led_id} not available. Must be in {self.LED_ID}.")
        if self._is_equal_led_config(led_id=led_id, feedback_mode=feedback_mode, intensity=intensity):
            LOGGER.debug(f"LED {led_id} already configured: {self._led_configs[led_id]}")
            return
        self.send_command(Command.format(Command.SETUP_LED, led_id, feedback_mode, intensity))
        self._led_configs[led_id]['mode'] = feedback_mode
        self._led_configs[led_id]['intensity'] = intensity

    def _setup_leds(self):
        for _led_id in self.LED_ID:
            self.setup_led(led_id=_led_id)

    def setup_magnet(self):
        self.send_command(Command.format(Command.SETUP_MAGNET))

    def setup_signal_dac(self):
        # TODO
        return

    def setup_signal_mode(self, index: int, repeat: int, dac: int, channel: int):
        self.send_command(Command.format(Command.SETUP_SIGNAL_MODE, index, repeat, dac, channel))

    def start_signal(self, index: int):
        self.send_command(Command.format(Command.START_SIGNAL, index))

    def stop_signal(self, index: int):
        self.send_command(Command.format(Command.STOP_SIGNAL, index))

    def scan_i2c(self):
        return self.send_command(Command.format(Command.SCAN_I2C))

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
