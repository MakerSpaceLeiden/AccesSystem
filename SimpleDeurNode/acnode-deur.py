#!/usr/bin/env python3.4
#
import time 
import sys
import os

# Mode (mosfet -or- stepper)
mosfet=1
stepper=0
steps=350

# in MOSFET mode
#
pin = 18
pin_high_time = 5	# in seconds

# In stepper/BCM mode
pin_dir=17     
pin_step =27
pin_enable=22
Sleep=0.0001

sys.path.append('../lib')
from SimpleACNode import SimpleACNode
from OfflineModeACNode import OfflineModeACNode

def init_gpio(self):
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
          GPIO.setup(pin, GPIO.OUT)
          GPIO.output(pin, False)

def open_door(self):
     if ( mosfet == 1 ): 
          GPIO.output(pin, True)     
          time.sleep(pin_high_time)
          GPIO.output(pin, False)
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

class DeurNode(SimpleACNode, OfflineModeACNode):
  command = "open"

  def __init__(self):
    super().__init__()
    self.commands[ 'revealtag' ] = self.cmd_revealtag

  def cmd_revealtag(self,path,node,nonce,payload):
    if not self.last_tag:
       self.logger.info("Asked to reveal a tag - but nothing swiped.")

    tag = '-'.join(str(int(bte)) for bte in self.last_tag)

    self.logger.info("Last tag swiped at {}: {}".format(self.cnf.node, tag))

  # We load the hardware related libraries late and
  # on demand; this allows for an '--offline' flag.
  #
  def setup(self):
    super().setup()

    if self.cnf.offline:
       self.logger.info("TEST: import MFRC522")
    else:
       # Note: The current MFC522 library claims pin22/GPIO25
       # as the reset pin -- set by the constant NRSTPD near
       # the start of the file.
       #
       import MFRC522
       MIFAREReader = MFRC522.MFRC522()

    if self.cnf.offline:
       self.logger.info("TEST: import RPI.GPIO")
    else:
       # Note: The current MFC522 library claims pin22/GPIO25
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
    
  last_tag = None
  tag = None

  def loop(self):
   super().loop()

   uid = None
   if self.cnf.offline:
     (status,TagType) = (None, None)
   else:
     (status,TagType) = MIFAREReader.MFRC522_Request(MIFAREReader.PICC_REQIDL)
     if status == MIFAREReader.MI_OK:
        (status,uid) = MIFAREReader.MFRC522_Anticoll()
        if status == MIFAREReader.MI_OK:
          logger.info("Swiped card "+'-'.join(map(str,uid)))
        else:
          uid = None
     
   if self.last_tag != uid and uid:
      localtime = time.asctime( time.localtime(time.time()) )
      self.logger.info(localtime + "     Card UID: "+'-'.join(map(str,uid)))
      self.last_tag = uid
      self.send_request(uid)

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
acnode = DeurNode()

if not acnode:
  sys.exit(1)

exitcode = acnode.run()

sys.exit(exitcode)

