"""Python client for the SyncBoard (Teensy 4.1) microscope controller."""

import logging

from syncboard.board import (
    AdcReading,
    AirReading,
    AirReference,
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
    sht40_celsius,
    sht40_rh_percent,
)
from syncboard.config import (
    ConfigError,
    LedConfig,
    MicroscopeConfig,
    load_config,
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
    "AirReading",
    "AirReference",
    "pt1000_celsius",
    "sht40_rh_percent",
    "sht40_celsius",
    "Frame",
    "CameraInputs",
    "Status",
    "LedMeasurement",
    "LedSetup",
    "MicroscopeConfig",
    "LedConfig",
    "ConfigError",
    "load_config",
    "SyncBoardError",
    "CommandError",
    "ProtocolError",
    "ResponseTimeout",
    "SyncBoardNotFound",
    "find_port",
]
