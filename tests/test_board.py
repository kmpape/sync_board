"""Board API tests against a stub transport that records requests."""

import pytest

from syncboard import CommandError
from syncboard.config import ConfigError, LedConfig, MicroscopeConfig
from syncboard.board import (
    AirReference,
    FieldSensorApi,
    Frame,
    ScreenerEnable,
    SignalMode,
    SyncBoard,
    pt1000_celsius,
    sht40_celsius,
    sht40_rh_percent,
)


class StubTransport:
    def __init__(self, replies=None):
        self.requests = []
        self.timeouts = []
        self.replies = dict(replies or {})

    def request(self, command, *args, timeout=None):
        self.requests.append((command, args))
        self.timeouts.append(timeout)
        return self.replies.get(command, ())

    def close(self):
        pass


@pytest.fixture
def board():
    return SyncBoard(StubTransport({
        "status": ("1", "1", "0", "1", "1"),
        "activity": ("0", "0"),  # nothing running
        "setupScreener": ("1", "1", "0"),  # PCA9685 + ADS7828 found, ADS124S08 missing
    }))


def test_status_parsing(board):
    status = board.status()
    assert status.enabled and status.led_board_attached
    assert not status.magnet_board_attached
    assert status.sync_mode == 1
    assert status.screener_board_attached


def test_signal_load_interleaves_values_and_delays(board):
    board.signals.load(2, values=[1.0, 0.0], delays_ms=[5, -1])
    command, args = board._t.requests[-1]
    assert command == "loadSignal"
    assert args == (2, 2, 1.0, 5.0, 0.0, -1.0)


def test_signal_record_runs_the_full_sequence(board):
    board._t.replies["readSignal"] = ("2", "0.5", "0.6")
    data = board.signals.record(SignalMode.ADC, channel=3, n_samples=2, interval_ms=1.0)
    assert data == [0.5, 0.6]
    commands = [command for command, _ in board._t.requests]
    assert commands == ["setupSignal", "loadSignalUniform", "startSignal",
                        "activity", "readSignal"]
    with pytest.raises(ValueError, match="recording"):
        board.signals.record(SignalMode.DAC, channel=1, n_samples=2, interval_ms=1.0)


def test_activity_parsing_and_wait_timeout(board):
    board._t.replies["activity"] = ("5", "1")  # signals 0 and 2 active, imaging running
    assert board.signals.is_active(0)
    assert not board.signals.is_active(1)
    assert board.signals.is_active(2)
    assert board.imaging.is_running()
    with pytest.raises(TimeoutError):
        board.signals.wait(0, timeout=0.03, poll_interval_s=0.01)
    board._t.replies["activity"] = ("0", "0")
    board.signals.wait(0, timeout=0.1)  # returns immediately


def test_signal_configure_sends_wire_values(board):
    board.signals.configure(0, SignalMode.DO, option=3, repeat=True, is_slave=False)
    assert board._t.requests[-1] == ("setupSignal", (0, 8, 3, True, False))


def test_imaging_configure_pads_to_four_frames(board):
    board.imaging.configure([Frame(led=5, exposure_ms=10), Frame(led=2, exposure_ms=5)])
    command, args = board._t.requests[-1]
    assert command == "setupImaging"
    assert args == (True, 5, 10.0, True, 2, 5.0, False, 0, 0.0, False, 0, 0.0)
    board.imaging.start()
    assert board._t.requests[-1] == ("startImaging", (2,))


def test_imaging_rejects_zero_exposure_without_camera_gating(board):
    with pytest.raises(ValueError, match="exposure_ms"):
        board.imaging.configure([Frame(led=5)])
    board.imaging.set_sync_mode(1, led_by_camera=True)
    board.imaging.configure([Frame(led=5)])  # now valid


def test_imaging_start_requires_configure(board):
    with pytest.raises(RuntimeError):
        board.imaging.start()


def test_gpio_direction_validation(board):
    with pytest.raises(ValueError):
        board.io.setup_gpio(25, direction="sideways")
    board.io.setup_gpio(25, direction="output")
    assert board._t.requests[-1] == ("setupGpio", (25, True, False))


def test_initialise_attaches_and_sets_up_the_screener(board):
    board.initialise(screener_board=True)
    assert board._t.requests == [
        ("disable", ()), ("attachLed", (False,)), ("attachMagnet", (False,)),
        ("attachScreener", (True,)), ("enable", ()), ("setupScreener", ()),
    ]


def test_screener_setup_reports_optional_chips(board):
    chips = board.screener.setup()
    assert chips.pca9685 and chips.ads7828 and not chips.ads124s08


def test_screener_all_off_attempts_every_step(board):
    def request(command, *args, timeout=None):
        board._t.requests.append((command, args))
        if command == "setScreenerPwm":
            raise CommandError(command, "the screener's PCA9685 (0x51) did not answer at setup")
        return ()
    board._t.request = request
    with pytest.raises(CommandError, match="PCA9685"):
        board.screener.all_off()
    assert [c for c, _ in board._t.requests] == ["setScreenerPwm", "setScreenerRfSwitch"]


def test_screener_outputs_send_wire_values(board):
    board.screener.set_switch(3, 0.5)
    board.screener.set_enable(ScreenerEnable.SENSORS)
    board.screener.set_rf_switch(True)
    board.screener.all_off()
    assert board._t.requests == [
        ("setScreenerSwitch", (3, 0.5)),
        ("setScreenerEnable", (3, True)),
        ("setScreenerRfSwitch", (True,)),
        ("setScreenerPwm", (0, 0.0)),
        ("setScreenerRfSwitch", (False,)),
    ]


def test_screener_adc_reading(board):
    board._t.replies["readScreenerAdc"] = ("4194304", "0.25")
    reading = board.screener.read_adc(2, 3)
    assert (reading.code, reading.ratio) == (4194304, 0.25)
    assert board._t.requests[-1] == ("readScreenerAdc", (2, 3, 2, 1))
    assert board.screener.read_adc_volts(2, 3) == pytest.approx(0.625)


def test_fluxgate_readings(board):
    field = board.screener.field_sensor
    board._t.replies["readFieldFluxgate"] = ("0.366", "0.3660", "0.366")
    assert field.read_fluxgate_volts(3) == pytest.approx([0.366] * 3)
    assert board._t.requests[-1] == ("readFieldFluxgate", (3,))  # firmware's default settle
    assert field.read_fluxgate_mt(3, settle_ms=40) == pytest.approx(0.5)  # 0.732 V/mT
    assert board._t.requests[-1] == ("readFieldFluxgate", (3, 40))
    # Three conversions plus the REFOUT check, after the settle.
    assert board._t.timeouts[-1] == pytest.approx(2.0 + 0.04 + 0.4)
    board._t.replies["readFieldFluxgate"] = ("1.2", "1.58")
    with pytest.raises(ValueError, match="rail"):
        field.read_fluxgate_mt(2)


def test_hall_readings(board):
    field = board.screener.field_sensor
    board._t.replies["readFieldHall"] = ("0.55", "0.55")
    assert field.read_hall_mt(2) == pytest.approx(10.0)  # 0.05 of 5 V at 25 mV/mT
    assert board._t.requests[-1] == ("readFieldHall", (2,))
    board._t.replies["readFieldHall"] = ("0.95",)
    with pytest.raises(ValueError, match="linear range"):
        field.read_hall_mt()
    board._t.replies["readFieldSupply"] = ("4.98",)
    assert field.read_supply(settle_ms=0) == pytest.approx(4.98)
    assert board._t.requests[-1] == ("readFieldSupply", (0,))


def test_field_sensor_constants_match_the_datasheets():
    assert FieldSensorApi.FLUXGATE_V_PER_MT == pytest.approx(0.732)
    assert FieldSensorApi.HALL_MT_PER_RATIO == pytest.approx(200.0)


def test_humidity_reading_references(board):
    air = board.screener.air_sensor
    board._t.replies["readAirHumidity"] = ("0.5", "0.4")
    reading = air.read_humidity()
    assert board._t.requests[-1] == ("readAirHumidity", (0,))
    assert reading.relative_humidity == pytest.approx(50.0)
    assert reading.temperature_c == pytest.approx(-66.875 + 218.75 * 0.4)
    # OFFSET replies volts relative to 2V5_Offset: 0 V is half the 5 V supply.
    board._t.replies["readAirHumidity"] = ("0.0", "-0.5")
    reading = air.read_humidity(AirReference.OFFSET, settle_ms=500)
    assert board._t.requests[-1] == ("readAirHumidity", (1, 500))
    assert (reading.rh_fraction, reading.t_fraction) == pytest.approx((0.5, 0.4))
    assert reading.relative_humidity == pytest.approx(50.0)


def test_sensor_rtds_use_their_channels(board):
    board._t.replies["readScreenerRtd"] = ("1000.0",)
    assert board.screener.field_sensor.read_temperature_c() == pytest.approx(0.0, abs=1e-9)
    assert board._t.requests[-1] == ("readScreenerRtd", (7,))
    board.screener.air_sensor.read_rtd_ohms(polarity=1)
    assert board._t.requests[-1] == ("readScreenerRtd", (8, True))


def test_sht40_transfer_functions():
    # 10-90 % of the supply spans 0-100 %RH and -45-130 C (datasheet eqs. 1, 2).
    assert sht40_rh_percent(0.1) == pytest.approx(0.0)
    assert sht40_rh_percent(0.9) == pytest.approx(100.0)
    assert sht40_celsius(0.1) == pytest.approx(-45.0)
    assert sht40_celsius(0.9) == pytest.approx(130.0)


def test_screener_rtd_request(board):
    board._t.replies["readScreenerRtd"] = ("1085.2",)
    assert board.screener.read_rtd(7) == pytest.approx(1085.2)
    assert board._t.requests[-1] == ("readScreenerRtd", (7,))
    assert board.screener.read_rtd(7, polarity=1) == pytest.approx(1085.2)
    assert board._t.requests[-1] == ("readScreenerRtd", (7, True))
    board.screener.read_rtd(7, polarity=0)
    assert board._t.requests[-1] == ("readScreenerRtd", (7, False))


def test_pt1000_celsius():
    assert pt1000_celsius(1000.0) == pytest.approx(0.0, abs=1e-9)
    assert pt1000_celsius(1385.055) == pytest.approx(100.0, abs=1e-3)
    assert pt1000_celsius(921.599) == pytest.approx(-20.0, abs=1e-2)


def test_led_calibrate_uses_the_microscope_config():
    config = MicroscopeConfig(leds={5: LedConfig(5, 7.5, "450 nm")})
    board = SyncBoard(StubTransport(), config)
    board.leds.calibrate(5)
    assert board._t.requests[-1] == ("calibrateLed", (5, 7.5))
    board.leds.calibrate(5, 2.0)  # explicit value wins
    assert board._t.requests[-1] == ("calibrateLed", (5, 2.0))


def test_led_calibrate_refuses_an_unconfigured_channel(board):
    with pytest.raises(ConfigError):
        board.leds.calibrate(5)
    assert not board._t.requests
    board.leds.calibrate(5, 1.0)
    assert board._t.requests[-1] == ("calibrateLed", (5, 1.0))
