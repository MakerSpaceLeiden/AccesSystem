#!/usr/bin/env python3.4
#
import time 
import sys
import os

sys.path.append('../lib')
from RfidReaderNode import RfidReaderNode

sys.path.append('../lib/Drivers')
from KrachtstroomHW import KrachtstroomHW

class KrachtstroomNode(RfidReaderNode,KrachtstroomHW):
  powered = 0
  machine = None
  machine_tag = None
  last_ok = 0
  user_tag = None
  ignore_time = 0
  ignore_timeout = 300

  default_grace = 6 	# seconds - timeout between card offer & cable offer.
  default_graceOff = 2 	# seconds -- timeout between removal of card and powerdown.

  command = 'energize'

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

  def cmd_approved(self,msg):
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

    # Parse the machine/tag pairs into a more convenient
    # dictionary.
    #
    if self.cnf.machines:
       n = {}
       for e in self.cnf.machines:
         machine, tag= e.split('=',1)
         if machine == None or tag == None:
            self.logger.debug("Malformed machine definition: {}. Aborting.".format(e));
            sys.exit(1)

         n[ tag ] = machine
         self.logger.info("Machine {} - {}".format(machine,tag))

       self.cnf.machines = n
    else:
       self.logger.critical("No machine tags known. Cannot operate. Aborting.");
       sys.exit(1)

    # Ready to start - turn top LED full on.
    self.logger.info("Ready to service (Top LED on)")
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

             self.logger.info("Machine " + self.machine + " now wired up, seen user tag - requesting permission")

             super().send_request(self.command, self.cnf.node, self.machine, self.user_tag)
             self.setBottomLED(50)
             self.ignore_time = 0
           else:
              if self.ignore_time == 0:
                 self.logger.info("Ignoring machine tag without user tag.")
                 self.ignore_time = time.time()
              else:
                 if time.time() - self.ignore_time > self.ignore_timeout:
                    self.logger.info("Still ignoring machine tag without user tag.")
                    self.ignore_time = time.time()
                 else:
                    self.logger.debug("Ignoring machine tag without user tag.")

              # flash the led for about a third of the grace time.
              #
              self.setBottomLED(8)
              self.last_ok = time.time() + self.cnf.grace - self.cnf.grace/3
              self.machine = None
         
         else:
           self.logger.debug("Assuming {} to be a user tag. Now waiting for machine tag.".format(tag))
           self.user_tag = uid
           self.setBottomLED(4)
           self.ignore_time = 0

           # Allow the time to move along if the user holds the
           # card long against the reader. So in effect the grace
           # period becomes the time between cards.
           #
           self.last_ok = time.time()
       else:
          if time.time() - self.last_ok > 20:
              self.logger.debug("No machine tag presented, going back to waiting for user tag.");
              self.user_tag = None
              self.machine = None
              self.last_ok = time.time()

       if time.time() - self.last_ok > self.cnf.grace:
          self.setBottomLED(0)

# Spin up a node; and run it forever; or until aborted; and
# provide a non-zero exit code as/if needd.
#
#
acnode = KrachtstroomNode()

if not acnode:
  sys.exit(1)

exitcode = acnode.run()
sys.exit(exitcode)

