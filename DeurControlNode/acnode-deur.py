#!/usr/bin/env python3.4
#
import time
import sys
import os

sys.path.append('../lib')
from ActuatorACNode import ActuatorACNode
from OfflineModeACNode import OfflineModeACNode

# enbale + config stepper / disable stepper
stepper=0
steps=350

# In BCM mode
pin_dir=17     
pin_step =27
pin_enable=22
Sleep=0.0001

def init_gpio():
     GPIO.setwarnings(False)
     GPIO.setmode(GPIO.BCM)
     if ( stepper == 1 ):
          GPIO.setup(pin_dir, GPIO.OUT)
          GPIO.setup(pin_step, GPIO.OUT)
          GPIO.setup(pin_enable, GPIO.OUT)
          GPIO.output(pin_enable, True)
          GPIO.output(pin_dir, False)
          GPIO.output(pin_step, False)
     if ( mosfet == 1 ):
          GPIO.setup(18, GPIO.OUT)
          GPIO.output(18, False)

def open_door():
     if ( mosfet == 1 ): 
          GPIO.output(18, True)     
          time.sleep(5)
          GPIO.output(18, False)
     if (stepper == 1 ):
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


class DeurControllerNode(ActuatorACNode, OfflineModeACNode):
  command = 'open'

  # Load the various libraries (unless in test/offline mode)
  #
  def setup(self):
   super().setup()

   if self.cnf.offline:
     self.logger.info("TEST: import RPI.GPIO")
   else:
     import RPi.GPIO as GPIO

   if self.cnf.offline:
     self.logger.info("TEST: init_gpio()")
   else:
     self.logger.debug("Initializing hardware.")
     init_gpio()

  def execute(self):
    if self.cnf.offline:
       self.logger.debug("TEST: open_door()")
    else:
       self.loggin.info("Oepning door")
       open_door()

  def on_exit(self,exitcode):
    if not self.cnf.offline:
      GPIO.cleanup()
    else:
      self.logger.debug("TEST: GPIO_cleanup() called.")
    super().on_exit(exitcode)

  def cmd_approved(self,path,node,theirbeat,payload):
   # Permit an 'alien' beat to open the door - i.e. one
   # originating from anohter unit.
   #
   self.last_tag_beat = theirbeat
   self.last_tag_shown = None
   super().cmd_approved(path,node,theirbeat,payload)

# Spin up a node; and run it forever; or until aborted; and
# provide a non-zero exit code as/if needd.
#
#
acnode = DeurControllerNode()

if not acnode:
  sys.exit(1)

exitcode = acnode.run()

sys.exit(exitcode)
