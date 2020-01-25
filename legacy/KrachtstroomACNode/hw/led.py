import RPIO 
import time

# Top and bottom LEDs
# - Flash alternating.
#
# Pin 16 / GPIO23
# Pin 17 / GPIO24

for pin in 23,24:
  print "Now flashing Pin " + str(pin)

  RPIO.setup(pin, RPIO.OUT)

  for cnt in range(1,5):
    print "."
    RPIO.output(pin, True)
    time.sleep(0.5)
    RPIO.output(pin, False)
    time.sleep(0.5)

  RPIO.setup(pin, RPIO.IN)

