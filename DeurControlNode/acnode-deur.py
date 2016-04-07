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
  default_renew = 300
  nonce_time = None

  def parseArguments(self):
    self.parser.add('--renew', action='store', default = self.default_renew,
                   help='Renewal rate of the nonce (default is {} seconds)'.format(self.default_renew))

    super().parseArguments()

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

    nonce = None

    # Update the nonce; as to disallow replay.
    self.roll_nonce()

  # Upon (re)connect - force a new nonce; thus ensuring
  # that the master can send us a requst with a nonce
  # that we accept.
  #
  def on_subscribe(self, client, userdata, mid, granted_qos):
    super.on_subscribe(client, userdata, id, granted_qos)
    self.roll_nonce()

  def loop(self):
   super().loop()

   if time.time() - nonce_time > self.cnf.renew:
     roll_nonce()
     self.logger.info("Rolling secret")
     nonce_time = time.time()

  def on_exit(self,exitcode):
    if not self.cnf.offline:
      GPIO.cleanup()
    else:
      self.logger.debug("TEST: GPIO_cleanup() called.")
    super().on_exit(exitcode)

# Spin up a node; and run it forever; or until aborted; and
# provide a non-zero exit code as/if needd.
#
#
acnode = DeurControllerNode()

if not acnode:
  sys.exit(1)

exitcode = acnode.run()

sys.exit(exitcode)

