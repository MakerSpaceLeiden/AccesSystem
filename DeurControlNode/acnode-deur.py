#!/usr/bin/env python3.4
#
import time
import sys
import os

sys.path.append('../lib')
from ActuatorACNode import ActuatorACNode
from OfflineModeACNode import OfflineModeACNode


from RfidReaderNode import RfidReaderNode

sys.path.append('../lib/Drivers')

# from Stepper import Stepper as Driver
from Mosfet import Mosfet as Driver

class DeurControllerNode(Driver,ActuatorACNode, OfflineModeACNode):
  command = 'open'

  def execute(self):
    self.loggin.info("Opening door")
    open_door()

  def cmd_approved(self,msg):
   # Permit an 'alien' beat to open the door - i.e. one
   # originating from anohter unit.
   #
   self.last_tag_beat = theirbeat
   self.last_tag_shown = None
   super().cmd_approved(path,msg)

# Spin up a node; and run it forever; or until aborted; and
# provide a non-zero exit code as/if needed.
#
acnode = DeurControllerNode()

if not acnode:
  sys.exit(1)

exitcode = acnode.run()

sys.exit(exitcode)
