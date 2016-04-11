#!/usr/bin/env python3.4
#
import time 
import sys
import os

sys.path.append('../../lib')
from ACNode import ACNode
from OfflineModeACNode import OfflineModeACNode

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

class KrachtstroomNode(OfflineModeACNode):
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
   power(0)
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
    if args.offline:
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

  def parseArguments(self):
    self.parser.add('--machines', action='append',
                   help='Machine/tag pairs - separated by a equal sign.')
    self.parser.add('--grace', action='store', default=self.default_grace,
                   help='Grace period between offering cards, in seconds (default: '+str(self.default_grace)+' seconds)')
    self.parser.add('--offdelay', action='store', default=self.default_graceOff,
                   help='Delay between loosing the machine card and powering down, in seconds (default: '+str(self.default_graceOff)+' seconds)')

    self.parser.add('tags', nargs='*',
       help = 'Zero or more test tags to "pretend" offer with 5 seconds pause. Once the last pair has been offered the daemon will enter the normal loop.')

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

  def on_message(self,client, userdata, message):
    payload = super().on_message(client, userdata, message)

    if not payload:
       return 

    try:
      what, which, result = payload.split()
    except:
      self.logger.warning("Cannot parse payload; ignored")
      return

    if what != self.machine:
      self.logger.info("Machine '{}' not connected (connected machine is {})- ignoring command".format(what,self.machine))
      return 

    if not which in self.cnf.machines:
      self.logger.info("Not in control of a machine called'{0}'- ignored".format(which))
      return 

    if what != self.command:
      self.logger.warning("Unexpected command '{}' - I can only '{}' a '{}' -- ignored".format(what,self.command.which))
      return

    if result == 'denied':
      self.logger.info("Denied XS")
      return 103

    if result != 'approved':
      self.logger.info("Unexpected result; ignored")
      return 104

    self.logger.info("Got the OK - Powering up the " + self.machine)

    self.setBottomLED(1)
    power(1)

    self.machine_tag = self.cnf['machines'][self.machine]
    self.powered = 1
    self.last_ok = time.time()

  def __init__(self):
    super().__init__()
    self.commands[ 'revealtag' ] = self.cmd_revealtag

  def cmd_revealtag(self,path,node,nonce,payload):
    if not self.user_tag:
       self.logger.info("Asked to reveal a tag - but nothing swiped.")

    tag = '-'.join(str(int(bte)) for bte in self.user_tag)
    self.logger.info("Last tag swiped at {}: {}".format(self.cnf.node,tag))

    # Shall we email here -- or at the master ??

  # We load the hardware related libraries late and
  # on demand; this allows for an '--offline' flag.
  #
  def setup(self):
    super().setup()
    self.initGPIO()

    # Ready to start - turn top LED full on.
    self.setTopLED(1)

  last_tag = None
  tag = None
  fake_time = 0
  subscribed = False

  def on_subscribe(self, client, userdata, mid, granted_qos):
      super().on_subscribe(client, userdata,mid,granted_qos)
      self.subscribed = True

  def loop(self):
   super().loop()

   uid = None
   tag = None
   if self.cnf.tags and time.time() - self.fake_time > 0.5 and self.subscribed:
      tag = self.cnf.tags.pop(0)
      self.logger.info("Pretending swipe of fake tag: <"+tag+">")
      if sys.version_info[0] < 3:
        uid = ''.join(chr(int(x)) for x in tag.split("-"))
      else:
        uid = bytearray(map(int,tag.split("-")))
      if not self.cnf.tags:
         self.logger.info("And that was the last pretend tag; going into normal mode after this.")
      self.fake_time = time.time()
   else:
     if self.cnf.offline:
       (status,TagType) = (None, None)
     else:
       (status,TagType) = MIFAREReader.MFRC522_Request(MIFAREReader.PICC_REQIDL)
       if status == MIFAREReader.MI_OK:
         (status,uid) = MIFAREReader.MFRC522_Anticoll()
         if status == MIFAREReader.MI_OK:
           tag = '-'.join(map(str,uid))
           self.logger.info("Detected card: " + tag)
         else:
           uid = None
     
   if self.powered:
       # check that the right plug tag is still being read.
       #
       if self.machine_tag == uid:
          self.logger.debug("plug tag still detected.")
          self.last_ok = time.time()

       if time.time() - self.last_ok > self.cnf.graceOff:
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
         if tag in self.cnf.machines:
           m = self.cnf.machines[ tag ]
           if self.user_tag:
             self.machine = m
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
           self.logger.debug("Assuming this is a user tag.")
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

