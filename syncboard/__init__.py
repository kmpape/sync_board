"""Python client for the SyncBoard (Teensy 4.1) microscope controller."""

import logging

from syncboard.board import (
    FeedbackMode,
    Frame,
    LedMeasurement,
    LedSetup,
    SignalMode,
    Status,
    SyncBoard,
)
from syncboard.protocol import (
    CommandError,
    ProtocolError,
    ResponseTimeout,
    SyncBoardError,
)
from syncboard.transport import SyncBoardNotFound, find_port

logging.getLogger("syncboard").addHandler(logging.NullHandler())

__all__ = [
    "SyncBoard",
    "SignalMode",
    "FeedbackMode",
    "Frame",
    "Status",
    "LedMeasurement",
    "LedSetup",
    "SyncBoardError",
    "CommandError",
    "ProtocolError",
    "ResponseTimeout",
    "SyncBoardNotFound",
    "find_port",
]
