#!/usr/bin/env python3.4
#
import time 
import sys
import os

sys.path.append('../../lib')
from RfidReaderNode import RfidReaderNode

# GPIO Wiring
#
topLed=23	# GPIO23, Pin 16
bottomLed=24	# GPIO24, Pin 18
relay=4		# GPIO4 (pin 7)

fastFlashFrequency = 8
slowFlashFrequency = 2

# Relay power reduction control
frequency=100 	# Hz
holdpwm=25	# percent
holdDelay=0.3 	# seconds

pulseInc = 50 # in Micro Seconds


ledChannel = 1
relayChannel = 0

relayFull = int(1e6/frequency/pulseInc - 1)
relayLow = max(1,int(relayFull * holdpwm / 100 -1))

ledFull = int(1e6/1/pulseInc - 1)

class KrachtstroomNode(RfidReaderNode):
  powered = 0
  machine = None
  machine_tag = None
  last_ok = 0
  user_tag = None

  default_grace = 6 	# seconds - timeout between card offer & cable offer.
  default_graceOff = 2 	# seconds -- timeout between removal of card and powerdown.

  command = 'energize'

  topLedTransitionsPerCycle = 0
  bottomLedTransitionsPerCycle = 0

  def initGPIO(self) :
    if self.cnf.offline:
      self.logger.info("TEST: import PWM")
      self.logger.info("TEST: import MFRC522")
      self.logger.info("TEST: initializing PWM")
      return

    # We're careful as to not import the 
    # RPIO itself; as it magically claims
    # pin 22; thus conflicting with MFRC522.
    #
    import RPIO.PWM as PWM

    PWM.set_loglevel(PWM.LOG_LEVEL_ERRORS)
    PWM.setup(pulseInc)
    PWM.init_channel(relayChannel, subcycle_time_us=int(1e6/frequency)) 
    PWM.init_channel(ledChannel, subcycle_time_us=int(1e6/1)) # Cycle time in microSeconds == 1 second

    # Flash top LED while we get our bearings.
    #
    self.setTopLED(20)

    # Note: The current MFC522 library claims pin22/GPIO25
    # as the reset pin -- set by the constant NRSTPD near
    # the start of the file.
    #
    import MFRC522
    MIFAREReader = MFRC522.MFRC522()

  def cleardownGPIO(self):
   self.power(0)
   self.setBottomLED(0)
   self.setTopLED(0)

   PWM.clear_channel_gpio(relayChannel,relay)
   PWM.clear_channel_gpio(ledChannel,topLed)
   PWM.clear_channel_gpio(ledChannel,bottomLed)
   
   # Shutdown all PWM and DMA activity
   PWM.cleanup()

  
  def setLEDs(self):

    if self.cnf.offline:
      self.logger.info("TEST: topled=%d bottomled=%d", self.topLedTransitionsPerCycle, self.bottomLedTransitionsPerCycle)
      return

    PWM.clear_channel(ledChannel)
    for pin, state in { topLed: self.topLedTransitionsPerCycle, bottomLed: self.bottomLedTransitionsPerCycle }.iteritems():
      if state:
        ds = ledFull / (state*2 - 1)
        for i in range(0,state):
          PWM.add_channel_pulse(ledChannel, pin, start=i*ds*2, width=ds)
      else:
        PWM.add_channel_pulse(ledChannel, pin, start=0, width=1)
  
  def setTopLED(self, state ):
   self.topLedTransitionsPerCycle=state
   self.setLEDs()

  def setBottomLED(self, state ):
   self.bottomLedTransitionsPerCycle=state
   self.setLEDs()

  def power(self,state):
    if self.cnf.offline:
        self.logger.info("TEST: setting power=%d", state)
        return

    if state:
        PWM.clear_channel(relayChannel)
        PWM.add_channel_pulse(relayChannel, relay, start=0, width=relayFull)
        time.sleep(holdDelay)
        PWM.clear_channel(relayChannel)
        PWM.add_channel_pulse(relayChannel, relay, start=0, width=relayLow)
    else:
        PWM.add_channel_pulse(relayChannel, relay, start=0, width=0)
        PWM.clear_channel(relayChannel)

  def __init__(self):
    super().__init__()
    self.commands[ 'approved' ] = self.cmd_approved
    self.commands[ 'revealtag' ] = self.cmd_revealtag

  def parseArguments(self):
    self.parser.add('--machines', action='append',
                   help='Machine/tag pairs - separated by a equal sign.')
    self.parser.add('--grace', action='store', default=self.default_grace,
                   help='Grace period between offering cards, in seconds (default: '+str(self.default_grace)+' seconds)')
    self.parser.add('--offdelay', action='store', default=self.default_graceOff,
                   help='Delay between loosing the machine card and powering down, in seconds (default: '+str(self.default_graceOff)+' seconds)')

    super().parseArguments()

    # Parse the machine/tag pairs into a more convenient
    # dictionary.
    #
    if self.cnf.machines:
       n = {}
       for e in self.cnf.machines:
         machine, tag= e.split('=',1)
         n[ tag ] = machine
       self.cnf.machines = n

  def cmd_approved(self,path,node,theirbeat,payload):
    self.logger.info("Got the OK - Powering up the " + self.machine)

    self.setBottomLED(1)
    self.power(1)

    self.powered = 1
    self.last_ok = time.time()

  # We load the hardware related libraries late and
  # on demand; this allows for an '--offline' flag.
  #
  def setup(self):
    super().setup()
    self.initGPIO()

    # Ready to start - turn top LED full on.
    self.setTopLED(1)

  def loop(self):
   super().loop()

   uid = self.readtag()
   if self.powered:
       # check that the right plug tag is still being read.
       #
       if self.machine_tag == uid:
          self.logger.debug("plug tag still detected.")
          self.last_ok = time.time()

       if time.time() - self.last_ok > self.cnf.offdelay:
          self.logger.info("Power down.")
          self.powered = 0
          self.user_tag = None
          self.machine = None
          self.power(self.powered)
          self.setBottomLED(0)
   else:
       # we are not powered - so waiting for a user tag or device tag.
       #
       if uid:
         tag = '-'.join(map(str,uid))
         if tag in self.cnf.machines:
           m = self.cnf.machines[ tag ]
           if self.user_tag:
             self.machine = m
             self.machine_tag = uid

             self.logger.info("Machine " + self.machine + " now wired up - requesting permission")

             super().send_request(self.command, self.cnf.node, self.machine, self.user_tag)
             self.setBottomLED(50)
           else:
              self.logger.info("Ignoring machine tag without user tag.")

              # flash the led for about a third of the grace time.
              #
              self.setBottomLED(8)
              self.last_ok = time.time() + self.cnf.grace - self.cnf.grace/3
              self.machine = None
         
         else:
           self.logger.debug("Assuming {} to be a user tag.".format(tag))
           self.user_tag = uid
           self.setBottomLED(4)

           # Allow the time to move along if the user holds the
           # card long against the reader. So in effect the grace
           # period becomes the time between cards.
           #
           self.last_ok = time.time()

       if time.time() - self.last_ok > self.cnf.grace:
          self.setBottomLED(0)

  def on_exit(self,exitcode):
    if not self.cnf.offline:
      cleardownGPIO()
    else:
      self.logger.debug("TEST: GPIO_cleanup() called.")
    super().on_exit(exitcode)

# Spin up a node; and run it forever; or until aborted; and
# provide a non-zero exit code as/if needd.
#
#
acnode = KrachtstroomNode()

if not acnode:
  sys.exit(1)

exitcode = acnode.run()
sys.exit(exitcode)

