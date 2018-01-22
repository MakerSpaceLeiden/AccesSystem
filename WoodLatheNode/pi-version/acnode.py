#!/usr/bin/env python3.4
#
import time 
import sys
import os

import RPi.GPIO as GPIO

sys.path.append('../lib')
from SensorACNode import SensorACNode
from ActuatorACNode import ActuatorACNode
from RfidReaderNode import RfidReaderNode

ledPin = 23
relayPin = 18

class Node(RfidReaderNode, SensorACNode, ActuatorACNode):
  command = "energize"
  powered = 0


  def setup(self):
    super().setup()

    GPIO.setwarnings(False) 
    GPIO.setmode(GPIO.BCM)

    GPIO.setup(relayPin, GPIO.OUT)
    GPIO.output(relayPin, False)

    GPIO.setup(ledPin, GPIO.OUT)
    GPIO.output(ledPin, True)

  def relay(self,onOff):
    GPIO.output(relayPin, onOff)

  def led(self,onOff):
    GPIO.output(ledPin, onOff)

  def on(self):
   self.powered = 1
   self.relay(1)
   self.led(1)

  def off(self):
   self.powered = 0
   self.relay(0)
   self.led(0)
    
  def execute(self):
   self.on()

  def on_exit(self):
   self.powered = 0
   self.led(0)
   self.relay(0)
   GPIO.cleanup()
   super().onexit()

  flash_led = 0     
  poweredup = 0

  def loop(self):
   super().loop()

   if not self.powered:
     if time.time() - self.flash_led > 1:
       self.led(1)

     if time.time() - self.flash_led > 1.5:
       self.led(0)
       self.flash_led = time.time()
 
   uid = self.readtag()
     
   if self.last_tag != uid and uid:
      localtime = time.asctime( time.localtime(time.time()) )
      self.logger.info(localtime + "     Card UID: "+'-'.join(map(str,uid)))
      self.led(1)
      self.last_tag = uid
      self.poweredup = time.time()
      self.send_request(self.command, self.cnf.node, self.cnf.machine, uid)

   if time.time() - self.poweredup > 10:
     if self.last_tag == uid:
        self.last_tag = None
        if self.powered:
           self.off()
   
   
# Spin up a node; and run it forever; or until aborted; and
# provide a non-zero exit code as/if needd.
#
#
acnode = Node()

if not acnode:
  sys.exit(1)

exitcode = acnode.run()

sys.exit(exitcode)
