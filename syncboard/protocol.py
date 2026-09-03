"""Wire protocol encoding/decoding (pure functions; no serial I/O here).

Every message is one ASCII line. Fields are ``/``-separated::

    request : $<tag>/<command>[/<arg>...]
    reply   : $<tag>/ok[/<value>...]        exactly one reply per request
              $<tag>/err/<message>          message may itself contain '/'
    async   : $0/log/<text>                 unsolicited firmware output

``tag`` is a positive integer chosen by the host and echoed by the firmware,
so replies are unambiguously matched to requests even after a timeout.
"""

from __future__ import annotations

from dataclasses import dataclass


class SyncBoardError(Exception):
    """Base class for all errors raised by this package."""


class ProtocolError(SyncBoardError):
    """The serial stream contained something that is not valid protocol."""


class ResponseTimeout(SyncBoardError):
    """No reply arrived in time.

    The board may still have executed the request; callers must not blindly
    retry commands with side effects.
    """


class CommandError(SyncBoardError):
    """The firmware answered a request with an err reply."""

    def __init__(self, command: str, message: str):
        super().__init__(f"{command}: {message}")
        self.command = command
        self.message = message


@dataclass(frozen=True)
class Message:
    """One parsed protocol line."""

    tag: int
    kind: str  # "ok", "err" or "log"
    fields: tuple[str, ...]  # values (ok), [message] (err/log)


def format_arg(value: bool | int | float) -> str:
    if isinstance(value, bool):
        return "1" if value else "0"
    if isinstance(value, int):
        return str(value)
    if isinstance(value, float):
        return format(value, ".7g")
    raise TypeError(f"unsupported argument type {type(value).__name__}: {value!r}")


def encode_request(tag: int, command: str, args: tuple = ()) -> bytes:
    if tag <= 0:
        raise ValueError("tag must be a positive integer")
    if not command or "/" in command:
        raise ValueError(f"invalid command name {command!r}")
    parts = [f"${tag}", command, *(format_arg(a) for a in args)]
    return ("/".join(parts) + "\n").encode("ascii")


def parse_line(line: str) -> Message:
    """Parses one received line (without its trailing newline)."""
    if not line.startswith("$"):
        raise ProtocolError(f"line does not start with '$': {line!r}")
    tag_str, _, rest = line[1:].partition("/")
    try:
        tag = int(tag_str)
    except ValueError:
        raise ProtocolError(f"invalid tag in line: {line!r}") from None

    kind, _, payload = rest.partition("/")
    if tag == 0:
        if kind != "log":
            raise ProtocolError(f"unexpected unsolicited message: {line!r}")
        return Message(0, "log", (payload,))
    if kind == "ok":
        fields = tuple(payload.split("/")) if payload else ()
        return Message(tag, "ok", fields)
    if kind == "err":
        return Message(tag, "err", (payload,))
    raise ProtocolError(f"unknown reply kind {kind!r} in line: {line!r}")
