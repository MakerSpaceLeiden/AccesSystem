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
from ACNode import ACNode

class SimpleACNode(ACNode):
  last_tag = None
  nonce_time = 0
  fake_time = 0

  def init_gpio(self):
     GPIO.setwarnings(False)
     GPIO.setmode(GPIO.BCM)
     if ( stepper == 1 ):
          logger.debug("setup stepper")
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
          logger.debug("open via mosfet")
          GPIO.output(pin, True)     
          time.sleep(pin_high_time)
          GPIO.output(pin, False)
     if (stepper == 1 ):
          logger.debug("open via stepper")
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

  # Firstly - we have an 'offline' mode that allows for
  # testing without the hardware (i.e. on any laptop or
  # machine with python); without the need for the stepper
  # motor, mosfet or RFID reader.
  #
  # In addition; have one extra argument - which allows us to 'fake'
  # swipe tags from the command line - and see the effect.
  #
  def parseArguments(self):
    self.parser.add('--offline', action='count',
                   help='Activate offline/no-hardware needed test mode; implies max-verbose mode.')
    self.parser.add('tags', nargs='*',
       help = 'Zero or more test tags to "pretend" offer with 5 seconds pause. Once the last one has been offered the daemon will enter the normal loop.')

    super().parseArguments()

  # We load the hardware related libraries late and
  # on demand; this allows for an '--offline' flag.
  #
  def setup(self):
    super().setup()

    # Go very verbose if we are in fake hardware or test-tag mode.
    #
    if self.cnf.offline or self.cnf.tags:
       self.cnf.v = 10

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

  def send_request(self,uid = None):
      super().send_request("open", self.cnf.machine,uid)

  def on_message(self,client, userdata, message):
    payload = super().on_message(client, userdata, message)

    if not payload:
       return 1

    # We know that we have a good message - so parse
    # it in our context.
    #
    try:
      what, which, result = payload.split()
    except:
      self.logger.warning("Cannot parse payload; ignored")
      return 100

    if which != self.cnf.machine:
      self.logger.info("I am a '{0}'- ignoring '{1}; ignored".format(which,self.cnf.machine))
      return 101

    if what != 'open':
      self.logger.warning("Unexpected command '{0}' - I can only <open> the <{1}>; ignored".format(what,self.cnf.machine))
      return 102

    if result == 'denied':
      self.logger.info("Denied XS")
      return 103

    if result != 'approved':
      self.logger.info("Unexpected result; ignored")
      return 104

    if self.cnf.offline:
       self.logger.debug("TEST: open_door()")
    else:
       self.loggin.info("Oepning door")
       open_door()

    return 0

  def loop(self):
   super().loop()

   if self.cnf.tags and time.time() - self.fake_time > 5:

     tag = self.cnf.tags.pop(0)
     self.logger.info("Pretending swipe of fake tag: <"+tag+">")

     if sys.version_info[0] < 3:
        tag_asbytes= ''.join(chr(int(x)) for x in tag.split("-"))
     else:
        tag_asbytes = bytearray(map(int,tag.split("-")))

     self.send_request(tag_asbytes)
     self.fake_time = time.time()

     if not self.cnf.tags:
        self.logger.info("And that was the last pretend tag; going into normal mode now.")

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
     
   if self.last_tag != uid:
      localtime = time.asctime( time.localtime(time.time()) )
      logger.info(localtime + "     Card UID: "+'-'.join(map(str,uid)))
      self.send_request(uid)
      self.last_tag = uid

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
acnode = SimpleACNode()

if not acnode:
  sys.exit(1)

exitcode = acnode.run()

sys.exit(exitcode)



