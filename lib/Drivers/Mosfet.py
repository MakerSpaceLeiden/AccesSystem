#!/usr/bin/env python3.4
#
import sys

# in Mosfet mode
#
pin = 18
pin_high_time = 5	# in seconds

sys.path.append('../../lib')
from GPIOACNode import GPIOACNode

class Mosfet(GPIOACNode):

  def setup(self):
    super().setup()

    if self.cnf.offline:
       self.logger.info("TEST: configuring mosfet hardware.")
       return

    self.logger.debug("Initializing mosfet hardware.")

    GPIO.setup(pin, GPIO.OUT)
    GPIO.output(pin, False)

  def open_door(self):
    if self.cnf.offline:
       self.logger.info("TEST: powering mosfet for {} seconds.".format(pin_high_time))
       return
   
    GPIO.output(pin, True)     
    time.sleep(pin_high_time)
    GPIO.output(pin, False)

if __name__ == "__main__":
  acnode = Mosfet()
  acnode.initialize()

  print("Opening door - MOSFET")
  acnode.open_door()
  print("Done.")

  sys.exit(0)

