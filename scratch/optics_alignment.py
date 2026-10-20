from syncboard import SyncBoard
from syncboard import FeedbackMode
from time import sleep

with SyncBoard.connect() as sb:          # auto-discovers the port
    sb.initialise(led_board=True)        # attach boards + enable
    
    # Params
    channel = 5
    feedback = FeedbackMode.CURRENT

    ## 1) Calibrate 
    # max_current_a = 7
    # sb.leds.calibrate(channel, max_current_a=max_current_a)

    # # 2) Turn on continuously, at low level (working for 450, 565)
    level = 0.05
    sb.leds.set_level(channel, level, feedback)
    sb.leds.on(channel)
    input("Press any key to stop")

    # # 3) Do a single pulse (for high intensity)
    # pulse_level = 0.8
    # pulse_ms = 100
    # sb.leds.set_level(channel, pulse_level, feedback)
    # sb.leds.pulse(channel, pulse_ms)
    # sleep(2)