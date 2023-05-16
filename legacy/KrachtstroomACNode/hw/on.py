import RPIO 
from RPIO import PWM
import time

PWM.setup()
PWM.init_channel(0)

pin=4 # pin 7 on the board, GPIO4

# Test script for the relay; powers it up
# at full PWM for a second; drops the voltage
# to about 10 volts (to reduce heat generation
# in the 330 ohm resistor) for 5 seconds; and 
# then switches # it off for a second.

PWM.add_channel_pulse(0, pin, start=0, width=2000-1)
print "Full on"
time.sleep(0.2)

print "Dropping back to 25%"
PWM.add_channel_pulse(0, pin, start=0, width=500)
time.sleep(2)

while True:
  time.sleep(1000);

PWM.cleanup()
