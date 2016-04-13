#!/usr/bin/env python3.4
#
import sys

sys.path.append('../lib')

from OfflineModeACNode import OfflineModeACNode

class GPIOACNode(OfflineModeACNode):

  def setup(self):
    super().setup()

    if self.cnf.offline:
      self.logger.info("TEST: import RPI.GPIO")
      return

    import RPi.GPIO as GPIO
    GPIO.setwarnings(False)
    self.logger.debug("GPIO configured.")

  def on_exit(self,exitcode):
    if not self.cnf.offline:
      GPIO.cleanup()
    else:
      self.logger.debug("TEST: GPIO_cleanup() called.")

    super().on_exit(exitcode)

