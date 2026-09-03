import pytest

from syncboard.protocol import ProtocolError, encode_request, format_arg, parse_line


def test_encode_request():
    assert encode_request(7, "setDac", (3, 1.25)) == b"$7/setDac/3/1.25\n"
    assert encode_request(1, "ping") == b"$1/ping\n"


def test_format_arg_types():
    assert format_arg(True) == "1"
    assert format_arg(False) == "0"
    assert format_arg(42) == "42"
    assert format_arg(0.1) == "0.1"
    assert format_arg(1e-7) == "1e-07"
    with pytest.raises(TypeError):
        format_arg("strings are not valid arguments")  # type: ignore[arg-type]


def test_parse_ok_reply():
    message = parse_line("$12/ok/3.3/1")
    assert (message.tag, message.kind) == (12, "ok")
    assert message.fields == ("3.3", "1")


def test_parse_ok_reply_without_values():
    assert parse_line("$5/ok").fields == ()


def test_parse_err_reply_keeps_slashes_in_message():
    message = parse_line("$3/err/bad thing: 1/2 of the args missing")
    assert message.kind == "err"
    assert message.fields == ("bad thing: 1/2 of the args missing",)


def test_parse_log_line():
    message = parse_line("$0/log/syncboard 2.0.0 booted")
    assert (message.tag, message.kind) == (0, "log")


@pytest.mark.parametrize("line", ["garbage", "$notatag/ok", "$4/wat/1", "$0/ok/1"])
def test_parse_rejects_invalid_lines(line):
    with pytest.raises(ProtocolError):
        parse_line(line)
