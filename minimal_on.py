from syncboard import SyncBoard
from syncboard import FeedbackMode
from time import sleep

with SyncBoard.connect() as sb:          # auto-discovers the port
    sb.initialise(led_board=True)        # attach boards + enable
    # sb.leds.calibrate(5, 10)
    # sb.leds.set_level(5, 0.1)
    # sb.leds.on(5)

    # sb.leds.set_level(5, 0.29, FeedbackMode.CURRENT)    

    # while True:
    #     sb.leds.pulse(5, 100)
    #     sleep(0.2)

    for p in range(1, 11):
        sb.leds.set_level(5, p / 10, FeedbackMode.CURRENT)
        sb.leds.pulse(5, 1000)
        sleep(0.1)
        print("="*20)
        print(f"LED 5 level: {p / 10}")
        for i in range(10):
            sleep(0.1)
            print(f"{sb.leds.measure_photodiode(5)}")
