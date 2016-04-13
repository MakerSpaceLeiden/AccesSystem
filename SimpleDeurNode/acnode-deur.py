#!/usr/bin/env python3.4
#
import time 
import sys
import os

sys.path.append('../lib')
from SensorACNode import SensorACNode
from ActuatorACNode import ActuatorACNode

from RfidReaderNode import RfidReaderNode

# from drivers.stepper import Stepper as Driver
sys.path.append('../lib/Drivers')
from Mosfet import Mosfet as Driver

class DeurNode(RfidReaderNode, SensorACNode, ActuatorACNode, Driver):
  command = "open"

  def execute(self):
   open_door()
    
  def loop(self):
   super().loop()

   uid = self.readtag()
     
   if self.last_tag != uid and uid:
      localtime = time.asctime( time.localtime(time.time()) )
      self.logger.info(localtime + "     Card UID: "+'-'.join(map(str,uid)))

      self.last_tag = uid
      self.send_request('open', self.cnf.node, self.cnf.machine, uid)

# Spin up a node; and run it forever; or until aborted; and
# provide a non-zero exit code as/if needd.
#
#
acnode = DeurNode()

if not acnode:
  sys.exit(1)

exitcode = acnode.run()

sys.exit(exitcode)
