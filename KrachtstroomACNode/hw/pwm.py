import RPIO 
from RPIO import PWM
import time

# Top and bottom LEDs
# - Flash alternating.
#

PWM.setup(1000) 
PWM.init_channel(0, subcycle_time_us=1000000)

PWM.add_channel_pulse(0, 23, start=500, width=499)
PWM.add_channel_pulse(0, 24, start=0, width=499)

time.sleep(5)

# Shutdown all PWM and DMA activity
PWM.cleanup()

