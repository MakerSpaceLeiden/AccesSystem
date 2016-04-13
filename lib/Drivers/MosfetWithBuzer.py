#!/usr/bin/env python3.4
#
import sys

mosfet_pin = 18	# mosfet
buzzer_pin = 7	# Buzzer

mosfet_pin_high_time = 5	# in seconds

sys.path.append('../../lib')
from GPIOACNode import GPIOACNode

class Mosfet(GPIOACNode):

  def setup(self):
    super().setup()

    if self.cnf.offline:
       self.logger.info("TEST: configuring mosfet and buzzer hardware.")
       return

    self.logger.debug("Initializing mosfet and buzzer hardware.")

    # Init mosfet
    GPIO.setup(mosfet_pin, GPIO.OUT)
    GPIO.output(mosfet_pin, False)

    # Init buzzer - and give a short beep.
    GPIO.setup(buzzer_pin, GPIO.OUT)
    GPIO.output(mosfet_pin, True)
    time.sleep(0.1)
    GPIO.output(mosfet_pin, False)

  def open_door(self):
    if self.cnf.offline:
       self.logger.info("TEST: powering mosfet and buzzer for {} seconds.".format(mosfet_pin_high_time))
       return
   
    GPIO.output(mosfet_pin, True)     
    GPIO.output(buzzer_pin, True)     
    time.sleep(mosfet_pin_high_time)
    GPIO.output(mosfet_pin, False)
    GPIO.output(buzzer_pin, False)     

if __name__ == "__main__":
  acnode = Mosfet()
  acnode.initialize()

  print("Opening door - MOSFET and BUZZER")
  acnode.open_door()
  print("Done.")

  sys.exit(0)

