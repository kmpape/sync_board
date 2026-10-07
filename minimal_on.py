from syncboard import SyncBoard
from time import sleep

with SyncBoard.connect() as sb:          # auto-discovers the port
    sb.initialise()        # attach boards + enable
    
    input("Press Enter to end the test...")