"""Python client for the SyncBoard (Teensy 4.1) microscope controller."""

import logging

from syncboard.board import (
    AdcReading,
    CameraInputs,
    FeedbackMode,
    Frame,
    LedMeasurement,
    LedSetup,
    ScreenerChips,
    ScreenerEnable,
    ScreenerRef,
    SignalMode,
    Status,
    SyncBoard,
    pt1000_celsius,
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
    "ScreenerChips",
    "ScreenerEnable",
    "ScreenerRef",
    "AdcReading",
    "pt1000_celsius",
    "Frame",
    "CameraInputs",
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
