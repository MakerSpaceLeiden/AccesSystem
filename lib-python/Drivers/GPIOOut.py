#!/usr/bin/env python3.4
#
import sys
import time

import RPi.GPIO as GPIO

sys.path.append('../../lib')
from GPIOACNode import GPIOACNode

class GPIOOut(GPIOACNode):
  name="Unknown"

  def setup(self):
    super().setup()

    if self.cnf.offline:
       self.logger.info("TEST: configuring hardware.")
       return

    self.logger.debug("Initializing hardware.")

    GPIO.setmode(GPIO.BCM)
    GPIO.setup(self.pin, GPIO.OUT)
    GPIO.output(self.pin, False)

  def gpioout(self,onOff):
    self.logger.info("GPIO[{}]::{} {}".format(self.pin, self.name, onOff))

    if self.cnf.offline:
      return
   
    GPIO.output(self.pin, onOff)

  def on_exit(self):
    GPIO.output(self.pin,False)
    GPIO.setup(self.pin, 0)
    super().on_exit()

