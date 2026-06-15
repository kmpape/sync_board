from datetime import datetime
import logging
from logging.handlers import RotatingFileHandler
from contextlib import contextmanager
from pathlib import Path
import serial
import time
from typing import List, Union
from termios import error as TermiosError

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
filename = "syncboard_serial_{}.log".format(datetime.now().strftime("%Y-%m-%d_%H:%M:%S.%f"))
# Log to a repository-relative folder so the package is importable on any
# machine. Previously hardcoded to /home/hslab/.../sync_board/Logs.
SYNC_BOARD_DIR = Path(__file__).resolve().parents[1]
LOG_DIR = SYNC_BOARD_DIR / "logs"
LOG_DIR.mkdir(parents=True, exist_ok=True)
file_handler = RotatingFileHandler(LOG_DIR / filename, maxBytes=1000000, backupCount=20)
file_handler.setFormatter(FORMATTER)
file_handler.setLevel(logging.INFO)
LOGGER.addHandler(file_handler)


class SerialConnection:
    NUM_SIG_FIG_FLOAT = 7
    DEBUG_MODE = False

    def __init__(
        self,
        port: str,
        baud_rate: int,
        num_data_bits: int = serial.EIGHTBITS,
        num_stop_bits: int = serial.STOPBITS_ONE,
        read_timeout_s: float = 1,
    ):
        LOGGER.debug(f"Connecting to {port} at {baud_rate} baud")

        self.port: str = port
        self.baud_rate: int = baud_rate
        self.num_data_bits: int = num_data_bits
        self.num_stop_bits: int = num_stop_bits
        self.read_timeout_s: float = read_timeout_s

        self.connection = serial.Serial(
            port=port,
            baudrate=baud_rate,
            bytesize=num_data_bits,
            stopbits=num_stop_bits,
            timeout=read_timeout_s,
        )

    @classmethod
    @contextmanager
    def connection(cls, *args, **kwargs):
        serial_connection = cls(*args, **kwargs)
        yield serial_connection
        serial_connection.disconnect()
        LOGGER.warning("SyncBoardController.SerialConnection: Disconnected from syncboard.")

    def reset_buffers(self):
        try:
            self.connection.reset_input_buffer()
            self.connection.reset_output_buffer()
        except TermiosError as e:
            LOGGER.warning(e)
            self.connection.close()
            self.connection.open()

    def send(self, data: bytes):
        self.reset_buffers()
        LOGGER.debug(f"Sending data: {data}")
        self.connection.write(data)

    def send_command(self, command: str) -> Union[None, List[str]]:
        encoded_command = command.encode()
        self.send(encoded_command)
        if self.DEBUG_MODE:
            responses = self.read_responses(wait_time=10)
        else:
            responses = self.read_responses(wait_time=0.05)

        if not responses:
            msg = f"SyncBoardController.SerialConnection: syncboard did not respond on command {command}."
            LOGGER.error(msg)
            raise serial.serialutil.SerialException(msg)
        else:
            msg = f"SyncBoardController.SerialConnection: syncboard responded with {responses} to {command}."
            LOGGER.info(msg)

        return responses

    def read_response(self, wait_time: float = 0) -> str:
        """
        Reads the response from a serial port. Returns an empty str if no data available.

        Parameters
        ----------
        wait_time: float
            Wait wait_time (in seconds) until the first response appears on the serial port.

        Returns
        -------
        response: str
            Stripped response string. Empty string if no response.
        """
        # TODO consider strings starting with "error/"
        response = self.connection.readline().decode()
        LOGGER.debug(f"Received: {response}")
        if wait_time > 0 and response == '':
            start_time = time.perf_counter()
            while time.perf_counter() - start_time < wait_time:
                if self.connection.in_waiting > 0:
                    response = self.connection.readline().decode()
                if response != '':
                    break
        return response.strip()

    def read_responses(self, wait_time: float) -> List[str]:
        data = []
        start_time = time.perf_counter()
        while time.perf_counter() - start_time < wait_time:
            if self.connection.in_waiting > 0:
                data.append(self.read_response(wait_time=wait_time))
        # response = self.connection.readline().decode()
        LOGGER.debug(f"Received: {data}")

        return data

    def disconnect(self):
        LOGGER.debug("Disconnecting from serial port...")
        self.connection.close()
        LOGGER.debug("Disconnected")
