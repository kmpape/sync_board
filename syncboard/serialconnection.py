import logging
from contextlib import contextmanager
import serial
import time
from typing import List, Union

LOGGER = logging.getLogger(__name__)


class SerialConnection:
    NUM_SIG_FIG_FLOAT = 7
    DEBUG_MODE = False
    def __init__(
        self,
        port: str,
        baud_rate: int,
        num_data_bits=serial.EIGHTBITS,
        num_stop_bits=serial.STOPBITS_ONE,
        read_timeout_s: float = 0.001,
    ):
        LOGGER.debug(f"Connecting to {port} at {baud_rate} baud")
        self.connection = serial.Serial(
            port=port,
            baudrate=baud_rate,
            bytesize=serial.EIGHTBITS,
            stopbits=serial.STOPBITS_ONE,
            timeout=read_timeout_s,
        )

    @classmethod
    @contextmanager
    def connection(cls, *args, **kwargs):
        serial_connection = cls(*args, **kwargs)
        yield serial_connection
        serial_connection.disconnect()

    def reset_buffers(self):
        self.connection.reset_input_buffer()
        self.connection.reset_output_buffer()

    def send(self, data: bytes):
        self.reset_buffers()
        LOGGER.debug(f"Sending data: {data}")
        self.connection.write(data)

    def send_command(self, command: str) -> Union[None, List[str]]:
        encoded_command = command.encode()
        self.send(encoded_command)
        if self.DEBUG_MODE:
            responses = self.read_responses(wait_time=0)
        else:
            return None

    def read_response(self) -> str:
        # TODO consider strings starting with "error/"
        response = self.connection.readline().decode()
        LOGGER.debug(f"Received: {response}")

        return response.strip()

    def read_responses(self, wait_time: float) -> List[str]:
        data = []
        start_time = time.perf_counter()
        while time.perf_counter() - start_time < wait_time:
            if self.connection.in_waiting > 0:
                data.append(self.read_response())
        response = self.connection.readline().decode()
        LOGGER.debug(f"Received: {response}")

        return data

    def disconnect(self):
        LOGGER.debug("Disconnecting from serial port...")
        self.connection.close()
        LOGGER.debug("Disconnected")
