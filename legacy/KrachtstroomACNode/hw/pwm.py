import RPIO 
from RPIO import PWM
import time

# Top and bottom LEDs
# - Flash alternating.
#

PWM.setup(1000) # Pulse increment in micro Seconds = 1 milli Second
PWM.init_channel(0, subcycle_time_us=1000000) # Cycle time in microSeconds == 1 second

# All values from here are in mSeconds.
#
PWM.add_channel_pulse(0, 23, start=500, width=499)
PWM.add_channel_pulse(0, 24, start=0, width=499)

time.sleep(5)

# Needed to clear down the GPIO back to input (cleanup() does not do that).
#
PWM.clear_channel_gpio(0,23)
PWM.clear_channel_gpio(0,24)

# Shutdown all PWM and DMA activity
PWM.cleanup()

