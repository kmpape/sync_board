from syncboard import SyncBoard
from syncboard import FeedbackMode
from time import sleep

with SyncBoard.connect() as sb:          # auto-discovers the port
    sb.initialise(led_board=True)        # attach boards + enable
    sb.leds.calibrate(5, 10)
    
    # Run this when adjusting the condenser lens distance
    # sb.leds.set_level(5, 1.0)
    # sb.leds.pulse(5, 10000)
    # input("Press Enter to continue...")

    # sb.leds.set_level(5, 0.29, FeedbackMode.CURRENT)    

    # while True:
    #     sb.leds.pulse(5, 100)
    #     sleep(0.2)

    # Run this when wanting to sweep LED power to see photodiode measurements
    for p in range(1, 11):
        sb.leds.set_level(5, p / 10, FeedbackMode.CURRENT)
        sb.leds.pulse(5, 1000)
        sleep(0.1)
        print("="*20)
        print(f"LED 5 level: {p / 10}")
        for i in range(10):
            sleep(0.1)
            print(f"{sb.leds.measure_photodiode(5)}")
