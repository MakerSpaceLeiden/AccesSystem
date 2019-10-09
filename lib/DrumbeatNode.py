#!/usr/bin/env python3.4

import os
import sys
import time
import hmac
import hashlib

sys.path.append('../lib')
import ACNode
class DrumbeatNode(ACNode.ACNode):
# import SharedSecret
# class DrumbeatNode(SharedSecret.SharedSecret):
  default_interval = 60
  default_node = "drumbeat"

  def parseArguments(self):
    self.parser.add('--interval','-i',default=self.default_interval,action='store',type=int,
         help='DrumbeatNode interval, in seconds (default: '+str(self.default_interval)+' seconds)'),

    super().parseArguments()

  last_time = 0
  def loop(self):
    if time.time() - self.last_time > self.cnf.interval:
      self.last_time = time.time()
      self.send(self.cnf.node, "beat")

      if self.cnf.secrets:
        for node in self.cnf.secrets.keys():
          self.send(node, "beat")

    super().loop()

# Allow this class to auto instanciate if
# we run it on its own.
#
if __name__ == "__main__":
  drumbeat = DrumbeatNode()
  if not drumbeat:
    sys.exit(1)
  exitcode = drumbeat.run()
  sys.exit(exitcode)

