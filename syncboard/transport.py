"""Serial transport: port discovery, framing, and the request/reply cycle."""

from __future__ import annotations

import logging
import threading
import time

import serial
import serial.tools.list_ports

from syncboard.protocol import (
    CommandError,
    Message,
    ProtocolError,
    ResponseTimeout,
    encode_request,
    parse_line,
)

logger = logging.getLogger("syncboard")

TEENSY_HWID = "16C0:0483"  # VID:PID of the SyncBoard's Teensy 4.1


def find_port(hwid: str = TEENSY_HWID) -> str:
    """Returns the device path of the first serial port matching ``hwid``."""
    for port in serial.tools.list_ports.comports():
        if f"VID:PID={hwid}" in port.hwid.upper():
            return port.device
    raise SyncBoardNotFound(f"no serial port with VID:PID {hwid} found")


class SyncBoardNotFound(ProtocolError):
    """No serial port matching the SyncBoard's USB id was found."""


class Transport:
    """A thread-safe, synchronous request/reply channel to the firmware.

    One request is in flight at a time (enforced with a lock). Unsolicited
    ``$0/log/...`` lines are forwarded to the ``syncboard`` logger whenever
    they are encountered.
    """

    DEFAULT_TIMEOUT_S = 2.0

    def __init__(self, port: str | None = None, baud_rate: int = 2_000_000):
        self.port = port if port is not None else find_port()
        # USB CDC ignores the baud rate, but pyserial wants one.
        self._serial = serial.Serial(port=self.port, baudrate=baud_rate, timeout=0.05)
        self._lock = threading.RLock()
        self._rx = bytearray()
        self._tag = 0
        logger.debug("connected to %s", self.port)

    def close(self) -> None:
        self._serial.close()

    def reconnect(self, port: str | None = None) -> None:
        """Reopens the connection, rediscovering the port by USB id.

        Useful when the board re-enumerates (e.g. after a power glitch) and
        reappears on a different device path. Board state is unknown after a
        reconnect; callers should re-run their bring-up.
        """
        with self._lock:
            try:
                self._serial.close()
            except Exception:
                pass
            self.port = port if port is not None else find_port()
            self._serial = serial.Serial(port=self.port, baudrate=self._serial.baudrate,
                                         timeout=0.05)
            self._rx.clear()
            logger.info("reconnected on %s", self.port)

    def __enter__(self) -> "Transport":
        return self

    def __exit__(self, *exc_info) -> None:
        self.close()

    def request(self, command: str, *args, timeout: float | None = None) -> tuple[str, ...]:
        """Sends one command and returns the fields of its ok reply.

        Raises CommandError on an err reply, ResponseTimeout if no reply
        arrives in time, and ProtocolError on garbage.
        """
        timeout = self.DEFAULT_TIMEOUT_S if timeout is None else timeout
        with self._lock:
            self._drain()
            self._tag += 1
            tag = self._tag
            self._serial.write(encode_request(tag, command, args))
            deadline = time.monotonic() + timeout
            while True:
                line = self._read_line(deadline)
                if line is None:
                    raise ResponseTimeout(
                        f"no reply to '{command}' within {timeout:.1f} s "
                        f"(the board may still have executed it)"
                    )
                message = self._parse(line)
                if message is None:
                    continue  # log line, non-protocol noise, or stale reply
                if message.kind == "err":
                    raise CommandError(command, message.fields[0])
                return message.fields

    # -- internals ----------------------------------------------------------

    def _parse(self, line: str) -> Message | None:
        """Parses one line, absorbing everything that is not the current
        request's reply: log lines, stale replies from timed-out requests,
        and non-protocol noise (all logged, never raised)."""
        try:
            message = parse_line(line)
        except ProtocolError:
            logger.warning("discarding non-protocol input: %r", line)
            return None
        if message.kind == "log":
            text = message.fields[0]
            if text.startswith("ERROR"):
                logger.error("firmware: %s", text)
            elif text.startswith("WARNING"):
                logger.warning("firmware: %s", text)
            else:
                logger.info("firmware: %s", text)
            return None
        if message.tag != self._tag:
            logger.warning("discarding stale reply: %s", line)
            return None
        return message

    def _pop_line(self) -> str | None:
        """Removes and returns the next complete line in the buffer, if any."""
        newline = self._rx.find(b"\n")
        if newline == -1:
            return None
        raw = self._rx[:newline]
        del self._rx[: newline + 1]
        return raw.rstrip(b"\r").decode("ascii", errors="replace")

    def _read_line(self, deadline: float) -> str | None:
        """Returns the next complete line, or None once ``deadline`` passes."""
        while True:
            line = self._pop_line()
            if line is not None:
                return line
            if time.monotonic() >= deadline:
                return None
            waiting = self._serial.in_waiting
            self._rx.extend(self._serial.read(waiting if waiting > 0 else 1))

    def _drain(self) -> None:
        """Processes any pending input (logs, stale replies) without blocking."""
        waiting = self._serial.in_waiting
        if waiting:
            self._rx.extend(self._serial.read(waiting))
        while (line := self._pop_line()) is not None:
            if line:
                self._parse(line)
