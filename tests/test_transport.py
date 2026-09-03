"""Transport tests against a scripted fake serial port."""

import pytest

from syncboard.protocol import CommandError, ResponseTimeout
from syncboard.transport import Transport


class FakeSerial:
    """Answers each written request from a queue of canned reply templates.

    ``{tag}`` in a template is replaced by the request's tag.
    """

    def __init__(self, replies):
        self.replies = list(replies)
        self.written = []
        self.rx = b""

    def write(self, data):
        self.written.append(data)
        tag = data.decode().lstrip("$").split("/", 1)[0]
        if self.replies:
            self.rx += self.replies.pop(0).format(tag=tag).encode()

    @property
    def in_waiting(self):
        return len(self.rx)

    def read(self, n):
        chunk, self.rx = self.rx[:n], self.rx[n:]
        return chunk

    def close(self):
        pass


def make_transport(replies):
    transport = Transport.__new__(Transport)
    import threading

    transport._serial = FakeSerial(replies)
    transport._lock = threading.RLock()
    transport._rx = bytearray()
    transport._tag = 0
    return transport


def test_request_returns_ok_fields():
    transport = make_transport(["${tag}/ok/3.14/7\n"])
    assert transport.request("readAdc", 1) == ("3.14", "7")
    assert transport._serial.written == [b"$1/readAdc/1\n"]


def test_err_reply_raises_command_error():
    transport = make_transport(["${tag}/err/system is disabled\n"])
    with pytest.raises(CommandError, match="system is disabled"):
        transport.request("setDac", 1, 1.0)


def test_log_lines_are_skipped_and_forwarded(caplog):
    transport = make_transport(["$0/log/WARNING: something\n${tag}/ok\n"])
    with caplog.at_level("WARNING", logger="syncboard"):
        assert transport.request("enable") == ()
    assert "something" in caplog.text


def test_stale_reply_from_previous_request_is_discarded():
    # Reply tagged 1 arrives only after request 2 was sent (a timeout race).
    transport = make_transport(["", "$1/ok/stale\n${tag}/ok/fresh\n"])
    with pytest.raises(ResponseTimeout):
        transport.request("ping", timeout=0.05)
    assert transport.request("ping") == ("fresh",)


def test_timeout_raises():
    transport = make_transport([""])
    with pytest.raises(ResponseTimeout):
        transport.request("ping", timeout=0.05)
