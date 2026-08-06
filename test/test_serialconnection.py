"""Transport tests that run without a physical board or pyserial installed."""

import importlib.util
from pathlib import Path
import sys
import types
import unittest


class FakeSerialPort:
    def __init__(self, *args, **kwargs):
        self.timeout = kwargs["timeout"]
        self._incoming = bytearray()
        self.writes = []
        self.reply_on_write = b""
        self.closed = False

    @property
    def in_waiting(self):
        return len(self._incoming)

    def write(self, data):
        self.writes.append(data)
        self._incoming.extend(self.reply_on_write)
        return len(data)

    def read(self, size):
        data = bytes(self._incoming[:size])
        del self._incoming[:size]
        return data

    def reset_input_buffer(self):
        self._incoming.clear()

    def close(self):
        self.closed = True

    def open(self):
        self.closed = False


def install_fake_serial():
    serial = types.ModuleType("serial")

    class SerialException(Exception):
        pass

    serial.SerialException = SerialException
    serial.SerialTimeoutException = SerialException
    serial.EIGHTBITS = 8
    serial.STOPBITS_ONE = 1
    serial.Serial = FakeSerialPort
    serial.serialutil = types.SimpleNamespace(SerialException=SerialException)
    serial.__path__ = []
    serial_tools = types.ModuleType("serial.tools")
    serial_tools.__path__ = []
    serial_list_ports = types.ModuleType("serial.tools.list_ports")
    serial_list_ports.comports = lambda: []
    serial.tools = serial_tools
    sys.modules["serial"] = serial
    sys.modules["serial.tools"] = serial_tools
    sys.modules["serial.tools.list_ports"] = serial_list_ports
    return serial


class SerialConnectionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.serial = install_fake_serial()
        module_path = Path(__file__).parents[1] / "syncboard" / "serialconnection.py"
        spec = importlib.util.spec_from_file_location("test_serialconnection_module", module_path)
        cls.transport = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.transport)

    def new_connection(self):
        return self.transport.SerialConnection("fake", 2_000_000)

    def test_returns_at_protocol_delimiter_without_waiting_for_quiet_period(self):
        connection = self.new_connection()
        connection.connection.reply_on_write = b"debug output\r\n$setDAC/1#%\r\n"

        self.assertEqual(connection.send_command("$setDAC/1#%"), "$setDAC/1#%")
        self.assertEqual(connection.connection.writes, [b"$setDAC/1#%"])

    def test_ignores_intermediate_frame_until_named_completion_frame(self):
        connection = self.new_connection()
        connection.connection.reply_on_write = (
            b"$Received setup LED command/1#%$setupLED/3#%"
        )

        self.assertEqual(
            connection.send_command("$setupLED/3/0/0.1#%", expected_response="setupLED"),
            "$setupLED/3#%",
        )

    def test_waits_for_legacy_status_as_a_completion_barrier(self):
        connection = self.new_connection()
        connection.connection.reply_on_write = b"Enabled GPIOs and level shifters\r\nSystem enabled\r\n"

        response = connection.send_and_wait_for_text(
            "$systemEnable#%",
            expected_text="System enabled",
            response_timeout_s=0.1,
        )
        self.assertIn("System enabled", response)

    def test_timeout_marks_connection_unsynchronised(self):
        connection = self.new_connection()

        with self.assertRaises(self.transport.SerialResponseTimeout):
            connection.send_command("$setDAC/1#%", response_timeout_s=0.001)
        with self.assertRaises(self.transport.SerialProtocolError):
            connection.send(b"$setDAC/1#%")


class RecordingConnection:
    def __init__(self):
        self.sent = []
        self.requested = []
        self.text_requests = []

    def send(self, data):
        self.sent.append(data)

    def send_command(self, command, response_timeout_s, expected_response):
        self.requested.append((command, response_timeout_s, expected_response))
        if expected_response == "Read value ":
            return "$Read value /2.5#%"
        return "${}/1#%".format(expected_response)

    def send_and_wait_for_text(self, command, expected_text, response_timeout_s):
        self.text_requests.append((command, expected_text, response_timeout_s))
        return expected_text


class ControllerProtocolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        install_fake_serial()
        for module_name in ("syncboard", "syncboard.serialconnection", "syncboard.syncboardcontroller"):
            sys.modules.pop(module_name, None)
        cls.controller_module = __import__("syncboard.syncboardcontroller", fromlist=["SyncBoardController"])

    def new_controller(self):
        connection = RecordingConnection()
        controller = self.controller_module.SyncBoardController(connection, "fake", 2_000_000)
        return controller, connection

    def test_one_way_firmware_command_does_not_wait_for_a_reply(self):
        controller, connection = self.new_controller()
        controller.enable_system()
        controller.setup_gpio(13, enable=1, mode=0, output_state=0)

        self.assertEqual(connection.text_requests, [("$systemEnable#%", "System enabled", 2.0)])
        self.assertEqual(connection.sent, [b"$setupGPIO/13/1/0/0#%"])
        self.assertEqual(connection.requested, [])

    def test_legacy_reply_name_is_matched_explicitly(self):
        controller, connection = self.new_controller()
        controller.set_magnet_current(0.25)

        self.assertEqual(connection.requested[0][2], "Set magnet current")

    def test_read_uses_named_response_and_parses_value(self):
        controller, connection = self.new_controller()

        self.assertEqual(controller.read_hall(1), 2.5)
        self.assertEqual(connection.requested[0][2], "Read value ")


if __name__ == "__main__":
    unittest.main()
