"""Opt-in integration tests for a physically connected SyncBoard.

These tests drive real output pins.  They are deliberately skipped unless the
operator explicitly supplies the port and the pins/channels that are safe to
toggle on their connected hardware.
"""

import os
import time
import unittest


try:
    from syncboard.com import get_syncboard_port
    from syncboard.syncboardcontroller import SyncBoardController
except ImportError:
    # Keeps the normal unit suite runnable in environments without pyserial.
    SyncBoardController = None
    get_syncboard_port = None


HARDWARE_TESTS_ENABLED = os.environ.get("SYNCBOARD_HARDWARE_TESTS") == "1"
SYNCBOARD_PORT = os.environ.get("SYNCBOARD_PORT")
DO_CHANNEL = os.environ.get("SYNCBOARD_TEST_DO_CHANNEL")
GPIO_PIN = os.environ.get("SYNCBOARD_TEST_GPIO")
DWELL_S = float(os.environ.get("SYNCBOARD_OUTPUT_DWELL_S", "0.01"))

_SKIP_REASON = (
    "real hardware tests require SYNCBOARD_HARDWARE_TESTS=1, "
    "SYNCBOARD_TEST_DO_CHANNEL, and SYNCBOARD_TEST_GPIO"
)


@unittest.skipUnless(
    SyncBoardController is not None and HARDWARE_TESTS_ENABLED and DO_CHANNEL and GPIO_PIN,
    _SKIP_REASON,
)
class SyncBoardHardwareIOTests(unittest.TestCase):
    """Verify that output-write commands reach the board and are acknowledged.

    The assertion is the firmware acknowledgement, proving it received and
    executed each write.  To assert voltage levels too, connect the selected
    output to independent measurement equipment or a suitable loopback.
    """

    @classmethod
    def setUpClass(cls):
        cls.do_channel = int(DO_CHANNEL)
        cls.gpio_pin = int(GPIO_PIN)
        # An explicit port is useful for a multi-device rig.  Otherwise use
        # the repository's VID:PID-based SyncBoard discovery helper.
        port = SYNCBOARD_PORT or get_syncboard_port()
        cls.board = SyncBoardController.from_serial_port(port)

    @classmethod
    def tearDownClass(cls):
        if not hasattr(cls, "board"):
            return
        try:
            # Make the pins safe even if a test assertion failed midway.
            cls.board.write_do(cls.do_channel, 0)
        except Exception:
            pass
        try:
            cls.board.write_gpio(cls.gpio_pin, 0)
        except Exception:
            pass
        try:
            cls.board.disable_system()
        finally:
            cls.board.connection.disconnect()

    def _write_and_assert_ack(self, write, expected_command):
        response = write()
        self.assertTrue(
            response.startswith("${}/".format(expected_command)),
            "Unexpected SyncBoard reply: {!r}".format(response),
        )
        time.sleep(DWELL_S)

    def test_write_do_toggles_selected_output(self):
        """Drive the selected DO low, high, then low again."""
        self.board.enable_system()
        try:
            for state in (0, 1, 0):
                self._write_and_assert_ack(
                    lambda state=state: self.board.write_do(self.do_channel, state),
                    "writeDO",
                )
        finally:
            self.board.write_do(self.do_channel, 0)

    def test_write_gpio_toggles_configured_digital_output(self):
        """Configure the selected GPIO as an output, then drive low/high/low."""
        self.board.disable_system()
        # setupGPIO's final argument is ``input_state``; false makes an output.
        self.board.setup_gpio(self.gpio_pin, enable=1, mode=0, output_state=0)
        self.board.enable_system()
        try:
            for state in (0, 1, 0):
                self._write_and_assert_ack(
                    lambda state=state: self.board.write_gpio(self.gpio_pin, state),
                    "writeGPIO",
                )
        finally:
            self.board.write_gpio(self.gpio_pin, 0)
            self.board.disable_system()


if __name__ == "__main__":
    unittest.main()
