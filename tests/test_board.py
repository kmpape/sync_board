"""Board API tests against a stub transport that records requests."""

import pytest

from syncboard.board import Frame, SignalMode, SyncBoard


class StubTransport:
    def __init__(self, replies=None):
        self.requests = []
        self.replies = dict(replies or {})

    def request(self, command, *args, timeout=None):
        self.requests.append((command, args))
        return self.replies.get(command, ())

    def close(self):
        pass


@pytest.fixture
def board():
    return SyncBoard(StubTransport({"status": ("1", "1", "0", "1")}))


def test_status_parsing(board):
    status = board.status()
    assert status.enabled and status.led_board_attached
    assert not status.magnet_board_attached
    assert status.sync_mode == 1


def test_signal_load_interleaves_values_and_delays(board):
    board.signals.load(2, values=[1.0, 0.0], delays_ms=[5, -1])
    command, args = board._t.requests[-1]
    assert command == "loadSignal"
    assert args == (2, 2, 1.0, 5.0, 0.0, -1.0)


def test_signal_configure_sends_wire_values(board):
    board.signals.configure(0, SignalMode.DO, option=3, repeat=True, is_slave=False)
    assert board._t.requests[-1] == ("setupSignal", (0, 8, 3, True, False))


def test_imaging_configure_pads_to_four_frames(board):
    board.imaging.configure([Frame(led=5, exposure_ms=10), Frame(led=2)])
    command, args = board._t.requests[-1]
    assert command == "setupImaging"
    assert args == (True, 5, 10.0, True, 2, 0.0, False, 0, 0.0, False, 0, 0.0)
    board.imaging.start()
    assert board._t.requests[-1] == ("startImaging", (2,))


def test_imaging_start_requires_configure(board):
    with pytest.raises(RuntimeError):
        board.imaging.start()


def test_gpio_direction_validation(board):
    with pytest.raises(ValueError):
        board.io.setup_gpio(25, direction="sideways")
    board.io.setup_gpio(25, direction="output")
    assert board._t.requests[-1] == ("setupGpio", (25, True, False))
