#!/usr/bin/env python3.4
#
import time 
import sys
import os

# In stepper/BCM mode
pin_dir=17     
pin_step =27
pin_enable=22
Sleep=0.0001

sys.path.append('../../lib')

from GPIOACNode import GPIOACNode

class Stepper(GPIOACNode):

  def setup(self):
    super().setup()

    if self.cnf.offline:
       self.logger.info("TEST: import RPI.GPIO - stepper stepper")
       return

    GPIO.setmode(GPIO.BCM)

    GPIO.setup(pin_dir, GPIO.OUT)
    GPIO.setup(pin_step, GPIO.OUT)
    GPIO.setup(pin_enable, GPIO.OUT)
    GPIO.output(pin_enable, True)
    GPIO.output(pin_dir, False)
    GPIO.output(pin_step, False)

    self.logger.debug("Configured stepper GPIO hardware.")

  def open_door(self):
    if self.cnf.offline:
      self.logger.info("TEST: moving stepper motor..")
      return

    GPIO.output(pin_enable,False)

    for step in xrange(steps):
               GPIO.output(pin_dir,False)     
               GPIO.output(pin_step,True)
               GPIO.output(pin_step,False)
               time.sleep(0.0009)
                    
    time.sleep(0.01)
    for step in xrange(steps):
               GPIO.output(pin_dir,True)     
               GPIO.output(pin_step,True)
               time.sleep(Sleep)
               GPIO.output(pin_step,False)
               time.sleep(0.0009)
                    
    GPIO.output(pin_dir,False)     
    time.sleep(0.1)
    GPIO.output(pin_enable,True)

if __name__ == "__main__":
  acnode = Stepper()
  acnode.initialize()

  print("Opening door - STEPPER")
  acnode.open_door()
  print("Done.")

  sys.exit(0)


