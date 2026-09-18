from syncboard import SyncBoard, FeedbackMode

import time

LED_450_NM = 5  # LED channel

# Auto-discovers the port; leaving the block disables the system and closes the port.
with SyncBoard.connect() as sb:
    sb.initialise(led_board=True)  # disable -> attach LED board -> enable

    # Once per LED after flashing the v2 firmware (no default max current for channel 5).
    # sb.leds.calibrate(LED_450_NM, max_current_a=10.0)

    # sb.leds.set_level(LED_450_NM, 0.1, feedback=FeedbackMode.OPTICAL)
    sb.leds.set_level(LED_450_NM, 0.1)

    while True:
        sb.leds.pulse(LED_450_NM, duration_ms=5)  # timed by the firmware
        # print(f"{sb.leds.measure_photodiode(LED_450_NM):.7f}")
        time.sleep(1)
