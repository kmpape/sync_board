import logging
from contextlib import contextmanager
import serial
import time
from typing import Optional

import serial
from termios import error as TermiosError

LOGGER = logging.getLogger(__name__)


class SerialResponseTimeout(serial.SerialException):
    """A request was written, but no complete protocol frame arrived in time.

    The board may have received and executed the request.  Callers must not
    resend it automatically because the protocol contains no request ID with
    which to distinguish a delayed reply from a reply to the retried command.
    """


class SerialProtocolError(serial.SerialException):
    """The serial stream exceeded a safe frame size without a valid frame."""


class SerialConnection:
    """A synchronous, framed connection to a SyncBoard.

    Calls are expected to be serialized by :class:`SyncBoardController`.  A
    reply is read until the firmware's ``%`` delimiter, so normal requests
    return as soon as their bytes arrive rather than after a fixed quiet time.
    """

    NUM_SIG_FIG_FLOAT = 7
    FRAME_START = b"$"
    FRAME_END = b"#%"
    # This controls only how long a missing reply is tolerated.  A normal
    # request returns immediately on ``#%``, so a generous deadline has no
    # effect on command latency while allowing the firmware to finish a busy
    # loop iteration before handling a request.
    DEFAULT_RESPONSE_TIMEOUT_S = 1.0
    DEFAULT_READ_TIMEOUT_S = 0.001
    MAX_FRAME_BYTES = 16 * 1024

    def __init__(
        self,
        port: str,
        baud_rate: int,
        num_data_bits: int = serial.EIGHTBITS,
        num_stop_bits: int = serial.STOPBITS_ONE,
        read_timeout_s: float = DEFAULT_READ_TIMEOUT_S,
    ):
        if read_timeout_s <= 0:
            raise ValueError("read_timeout_s must be greater than zero")

        LOGGER.debug("Connecting to %s at %s baud", port, baud_rate)
        self.port = port
        self.baud_rate = baud_rate
        self.num_data_bits = num_data_bits
        self.num_stop_bits = num_stop_bits
        self.read_timeout_s = read_timeout_s
        self._receive_buffer = bytearray()
        self._synchronised = True

        self.connection = serial.Serial(
            port=port,
            baudrate=baud_rate,
            bytesize=num_data_bits,
            stopbits=num_stop_bits,
            timeout=read_timeout_s,
        )

    @classmethod
    @contextmanager
    def open(cls, *args, **kwargs):
        """Open a connection for a ``with`` block."""
        serial_connection = cls(*args, **kwargs)
        try:
            yield serial_connection
        finally:
            serial_connection.disconnect()

    def reset_buffers(self):
        """Discard stale input only; never discard data queued for writing."""
        try:
            self._receive_buffer.clear()
            self.connection.reset_input_buffer()
        except TermiosError as exc:
            LOGGER.warning("Failed to reset serial input buffer: %s", exc)
            self.connection.close()
            self.connection.open()

    def send(self, data: bytes) -> None:
        """Write a command without expecting a response.

        This is only safe for firmware commands documented not to send a
        protocol reply.  Reply-producing commands must use ``send_command``;
        otherwise their reply could be consumed by a later request.
        """
        if not self._synchronised:
            raise SerialProtocolError("Serial connection is not synchronised; reconnect before sending")
        self.reset_buffers()
        LOGGER.debug("Sending data: %r", data)
        self.connection.write(data)

    def send_command(
        self,
        command: str,
        response_timeout_s: Optional[float] = None,
        expected_response: Optional[str] = None,
    ) -> str:
        """Send one command and return exactly one complete protocol reply."""
        timeout_s = self.DEFAULT_RESPONSE_TIMEOUT_S if response_timeout_s is None else response_timeout_s
        if timeout_s <= 0:
            raise ValueError("response_timeout_s must be greater than zero")
        self.send(command.encode("ascii"))
        return self.read_response(timeout_s, expected_response=expected_response)

    def send_and_wait_for_text(
        self,
        command: str,
        expected_text: str,
        response_timeout_s: Optional[float] = None,
    ) -> str:
        """Send a legacy one-way command and wait for its textual status.

        ``systemEnable`` and ``systemDisable`` predate the framed protocol but
        print an unambiguous completion status.  Waiting for that status is a
        real completion barrier, unlike sleeping for an arbitrary interval.
        """
        timeout_s = self.DEFAULT_RESPONSE_TIMEOUT_S if response_timeout_s is None else response_timeout_s
        if timeout_s <= 0:
            raise ValueError("response_timeout_s must be greater than zero")
        self.send(command.encode("ascii"))
        return self.read_text(expected_text, timeout_s)

    def read_response(
        self,
        response_timeout_s: Optional[float] = None,
        expected_response: Optional[str] = None,
    ) -> str:
        """Return the next ``$...#%`` frame before the total timeout expires."""
        timeout_s = self.DEFAULT_RESPONSE_TIMEOUT_S if response_timeout_s is None else response_timeout_s
        if timeout_s <= 0:
            raise ValueError("response_timeout_s must be greater than zero")
        if not self._synchronised:
            raise SerialProtocolError("Serial connection is not synchronised; reconnect before reading")

        deadline = time.monotonic() + timeout_s
        while True:
            frame = self._pop_frame()
            if frame is not None:
                response = frame.decode("ascii", errors="replace")
                response_name = self._frame_name(response)
                if expected_response is None or response_name in (expected_response, "error"):
                    LOGGER.debug("Received: %s", response)
                    return response
                # A few legacy commands emit an informational protocol frame
                # before their actual completion frame.  It is safe to ignore
                # only because the caller named the reply it is waiting for.
                LOGGER.debug("Ignoring intermediate protocol response: %s", response)

            if len(self._receive_buffer) > self.MAX_FRAME_BYTES:
                self._synchronised = False
                raise SerialProtocolError("Serial response exceeded maximum frame size")

            remaining_s = deadline - time.monotonic()
            if remaining_s <= 0:
                self._synchronised = False
                raise SerialResponseTimeout(
                    "SyncBoard did not send a complete protocol response within {:.3f} s".format(timeout_s)
                )

            # ``read(1)`` permits pyserial to block for at most the short
            # configured poll timeout.  Reading all currently buffered bytes
            # keeps long replies efficient without busy-spinning the CPU.
            waiting = self.connection.in_waiting
            chunk = self.connection.read(waiting if waiting > 0 else 1)
            if chunk:
                self._receive_buffer.extend(chunk)

    def read_text(self, expected_text: str, response_timeout_s: Optional[float] = None) -> str:
        """Wait for an unframed textual status line from legacy firmware."""
        timeout_s = self.DEFAULT_RESPONSE_TIMEOUT_S if response_timeout_s is None else response_timeout_s
        if timeout_s <= 0:
            raise ValueError("response_timeout_s must be greater than zero")
        expected = expected_text.encode("ascii")
        if not expected:
            raise ValueError("expected_text must not be empty")
        if not self._synchronised:
            raise SerialProtocolError("Serial connection is not synchronised; reconnect before reading")

        deadline = time.monotonic() + timeout_s
        while True:
            if expected in self._receive_buffer:
                response = self._receive_buffer.decode("ascii", errors="replace")
                LOGGER.debug("Received legacy status: %s", response)
                self._receive_buffer.clear()
                return response

            if len(self._receive_buffer) > self.MAX_FRAME_BYTES:
                self._synchronised = False
                raise SerialProtocolError("Serial status output exceeded maximum buffer size")

            if time.monotonic() >= deadline:
                self._synchronised = False
                raise SerialResponseTimeout(
                    "SyncBoard did not send {!r} within {:.3f} s".format(expected_text, timeout_s)
                )

            waiting = self.connection.in_waiting
            chunk = self.connection.read(waiting if waiting > 0 else 1)
            if chunk:
                self._receive_buffer.extend(chunk)

    def _pop_frame(self) -> Optional[bytes]:
        """Extract one valid frame, retaining any following bytes."""
        start = self._receive_buffer.find(self.FRAME_START)
        if start == -1:
            # Preserve a possible split start byte and discard debug output.
            if self._receive_buffer:
                LOGGER.debug("Discarding non-protocol serial output: %r", bytes(self._receive_buffer))
                self._receive_buffer.clear()
            return None
        if start:
            LOGGER.debug("Discarding non-protocol serial output: %r", bytes(self._receive_buffer[:start]))
            del self._receive_buffer[:start]

        end = self._receive_buffer.find(self.FRAME_END)
        if end == -1:
            return None
        end += len(self.FRAME_END)
        frame = bytes(self._receive_buffer[:end])
        del self._receive_buffer[:end]
        return frame

    @staticmethod
    def _frame_name(frame: str) -> str:
        """Return the command portion of a decoded ``$name/args#%`` frame."""
        payload = frame[1:-2]
        return payload.split("/", 1)[0].rstrip("#")

    def disconnect(self):
        LOGGER.debug("Disconnecting from serial port...")
        self.connection.close()
        LOGGER.debug("Disconnected")

    # Kept for callers of the original context-manager API.  Instances still
    # expose ``.connection`` as the underlying pyserial object.
    connection = open
