"""Microscope configuration file loading."""

import pytest

from syncboard.config import ConfigError, MicroscopeConfig, default_path, load_config, parse_config


def write(tmp_path, text):
    file = tmp_path / "microscope.toml"
    file.write_text(text)
    return file


def test_parses_led_entries(tmp_path):
    file = write(tmp_path, """
        [[leds]]
        channel = 5
        max_current_a = 7.5
        name = "450 nm"

        [[leds]]
        channel = 1
        max_current_a = 10
    """)
    cfg = load_config(file)
    assert cfg.path == file
    assert cfg.led(5).max_current_a == 7.5
    assert cfg.led(5).name == "450 nm"
    assert cfg.led(1).max_current_a == 10.0
    assert cfg.led(2) is None


def test_missing_default_file_is_empty(tmp_path, monkeypatch):
    monkeypatch.delenv("SYNCBOARD_CONFIG", raising=False)
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path))
    monkeypatch.chdir(tmp_path)  # no microscope.toml here
    assert default_path() == tmp_path / "syncboard" / "microscope.toml"
    cfg = load_config()
    assert cfg == MicroscopeConfig()


def test_working_directory_file_wins_over_user_config(tmp_path, monkeypatch):
    monkeypatch.delenv("SYNCBOARD_CONFIG", raising=False)
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "home"))
    user = tmp_path / "home" / "syncboard" / "microscope.toml"
    user.parent.mkdir(parents=True)
    user.write_text("[[leds]]\nchannel = 1\nmax_current_a = 1\n")
    monkeypatch.chdir(tmp_path)
    assert load_config().led(1).max_current_a == 1.0   # falls back to the user file
    local = write(tmp_path, "[[leds]]\nchannel = 1\nmax_current_a = 4\n")
    assert default_path() == local
    assert load_config().led(1).max_current_a == 4.0


def test_env_var_names_the_file(tmp_path, monkeypatch):
    file = write(tmp_path, "[[leds]]\nchannel = 3\nmax_current_a = 2\n")
    monkeypatch.setenv("SYNCBOARD_CONFIG", str(file))
    assert load_config().led(3).max_current_a == 2.0
    monkeypatch.setenv("SYNCBOARD_CONFIG", str(tmp_path / "absent.toml"))
    with pytest.raises(ConfigError):
        load_config()


def test_explicit_missing_file_raises(tmp_path):
    with pytest.raises(ConfigError):
        load_config(tmp_path / "absent.toml")


@pytest.mark.parametrize("text", [
    "leds = 5",
    "[[leds]]\nchannel = 5",
    "[[leds]]\nmax_current_a = 5",
    "[[leds]]\nchannel = 9\nmax_current_a = 5",
    "[[leds]]\nchannel = 0\nmax_current_a = 5",
    "[[leds]]\nchannel = 5\nmax_current_a = 0",
    "[[leds]]\nchannel = 5\nmax_current_a = true",
    "[[leds]]\nchannel = 5\nmax_current_a = 5\nname = 3",
    "[[leds]]\nchannel = 5\nmax_current_a = 5\n[[leds]]\nchannel = 5\nmax_current_a = 6",
    "this is not toml =",
])
def test_rejects_bad_files(tmp_path, text):
    with pytest.raises(ConfigError):
        load_config(write(tmp_path, text))


def test_parse_without_path():
    cfg = parse_config({"leds": [{"channel": 2, "max_current_a": 8}]})
    assert cfg.path is None and cfg.led(2).max_current_a == 8.0
