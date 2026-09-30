from syncboard import SyncBoard
from time import sleep

with SyncBoard.connect() as sb:          # auto-discovers the port
    sb.initialise(led_board=True)        # attach boards + enable
    # sb.leds.calibrate(5, 10)
    sb.leds.set_level(5, 0.1)

    sb.leds.on(5)

    # while True:
    #     # Read from photodiode
    #     pdval = sb.leds.measure_photodiode(5)
    #     print(f"Photodiode value: {pdval}")
    #     sleep(0.1)
    
    sb.leds.on(5)
    input("Press Enter to quit...")