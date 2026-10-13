"""Per-microscope configuration: what is physically installed on this
SyncBoard, as opposed to the board's own capabilities.

The client code is shared between microscopes, so anything that depends on
which LEDs are plugged in lives in a TOML file outside the package. The
file is looked up, in order, at:

1. the path passed explicitly to :func:`load_config` / ``SyncBoard.connect``;
2. ``$SYNCBOARD_CONFIG``;
3. ``microscope.toml`` in the current working directory (the repo root when
   scripts are run from there);
4. ``$XDG_CONFIG_HOME/syncboard/microscope.toml``
   (``~/.config/syncboard/microscope.toml`` by default).

A missing file is not an error: the configuration is then empty and
``leds.calibrate`` needs its max current passed explicitly. A file that
exists but cannot be parsed, or names a channel outside 1..8, raises
:class:`ConfigError`. See ``docs/microscope.example.toml`` for the format.
"""

from __future__ import annotations

import os
import tomllib
from dataclasses import dataclass, field
from pathlib import Path

ENV_VAR = "SYNCBOARD_CONFIG"
FILE_NAME = "microscope.toml"
DEFAULT_RELATIVE_PATH = Path("syncboard") / FILE_NAME

NUM_LED_CHANNELS = 8


class ConfigError(ValueError):
    """The microscope configuration file is unusable."""


@dataclass(frozen=True)
class LedConfig:
    channel: int
    max_current_a: float   # what calibrate() ramps to; level 1.0 is 95 % of it
    name: str = ""         # free text, e.g. "450 nm, Luminus CFT-50X-B"


@dataclass(frozen=True)
class MicroscopeConfig:
    leds: dict[int, LedConfig] = field(default_factory=dict)
    path: Path | None = None   # where it was loaded from; None if no file

    def led(self, channel: int) -> LedConfig | None:
        return self.leds.get(channel)


def user_config_path() -> Path:
    """``$XDG_CONFIG_HOME/syncboard/microscope.toml``."""
    base = os.environ.get("XDG_CONFIG_HOME")
    root = Path(base).expanduser() if base else Path.home() / ".config"
    return root / DEFAULT_RELATIVE_PATH


def default_path() -> Path:
    """The path consulted when no argument names a file: ``$SYNCBOARD_CONFIG``
    if set, else ``./microscope.toml`` if it exists, else the user config
    path."""
    env = os.environ.get(ENV_VAR)
    if env:
        return Path(env).expanduser()
    local = Path.cwd() / FILE_NAME
    if local.exists():
        return local
    return user_config_path()


def load_config(path: str | os.PathLike | None = None) -> MicroscopeConfig:
    """Loads the microscope configuration.

    With ``path`` the file must exist. Without it the default location is
    used and a missing file yields an empty configuration.
    """
    explicit = path is not None
    file = Path(path).expanduser() if explicit else default_path()
    if not file.exists():
        if explicit or os.environ.get(ENV_VAR):
            raise ConfigError(f"microscope config not found: {file}")
        return MicroscopeConfig()
    try:
        with open(file, "rb") as f:
            raw = tomllib.load(f)
    except tomllib.TOMLDecodeError as exc:
        raise ConfigError(f"{file}: {exc}") from exc
    return parse_config(raw, file)


def parse_config(raw: dict, path: Path | None = None) -> MicroscopeConfig:
    """Builds a :class:`MicroscopeConfig` from parsed TOML."""
    where = f"{path}: " if path else ""
    leds: dict[int, LedConfig] = {}
    entries = raw.get("leds", [])
    if not isinstance(entries, list):
        raise ConfigError(f"{where}'leds' must be an array of tables ([[leds]])")
    for i, entry in enumerate(entries):
        if not isinstance(entry, dict):
            raise ConfigError(f"{where}leds[{i}] must be a table")
        try:
            channel = entry["channel"]
            max_current = entry["max_current_a"]
        except KeyError as exc:
            raise ConfigError(f"{where}leds[{i}] needs {exc.args[0]!r}") from None
        if not isinstance(channel, int) or not 1 <= channel <= NUM_LED_CHANNELS:
            raise ConfigError(
                f"{where}leds[{i}]: channel must be an integer 1..{NUM_LED_CHANNELS}")
        if isinstance(max_current, bool) or not isinstance(max_current, (int, float)) \
                or max_current <= 0:
            raise ConfigError(f"{where}leds[{i}]: max_current_a must be a positive number")
        if channel in leds:
            raise ConfigError(f"{where}LED channel {channel} is listed twice")
        name = entry.get("name", "")
        if not isinstance(name, str):
            raise ConfigError(f"{where}leds[{i}]: name must be a string")
        leds[channel] = LedConfig(channel, float(max_current), name)
    return MicroscopeConfig(leds=leds, path=path)
