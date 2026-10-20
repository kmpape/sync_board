from syncboard import SyncBoard
from syncboard import FeedbackMode
from time import sleep

with SyncBoard.connect() as sb:          # auto-discovers the port
    sb.initialise(led_board=True)        # attach boards + enable
    # sb.leds.calibrate(5, max_current_a=7.5)
    
    
    # Run this when adjusting the condenser lens distance
    # sb.leds.set_level(5, 0.1)
    # sb.leds.pulse(5, 10000)
    # input("Press Enter to continue...")

    # sb.leds.set_level(5, 0.29, FeedbackMode.CURRENT)    

    # while True:
    #     sb.leds.pulse(5, 100)
    #     sleep(0.2)

    # Run this when wanting to sweep LED power to see photodiode measurements
    for p in range(1, 11):
        led_level = p / 10
        sb.leds.set_level(5, led_level, FeedbackMode.CURRENT)
        sb.leds.pulse(5, 1000)
        sleep(0.1)
        print("="*20)
        print(f"LED 5 level: {led_level}")
        for i in range(10):
            sleep(0.1)
            print(f"{sb.leds.measure_photodiode(5)}")

    # iterate over max current until 7.5 (rated LED current)
    # max_current_a = 1.0
    # for max_current_a in list(range(1, 8)) + [7.5]:
    #     sb.leds.calibrate(5, max_current_a)
    
    #     # Print the calibrated config
    #     sb.leds.set_level(5, 1.0)
    #     print(f"calibrated with: I={max_current_a}A: {sb.leds.get_setup(5)}")
    
    # # Print the calibrated config
    sb.leds.set_level(5, 1.0)
    print(f"{sb.leds.get_setup(5)}")