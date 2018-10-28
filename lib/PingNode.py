#!/usr/bin/env python3.4

import os
import sys
import time
import hmac
import hashlib

sys.path.append('../lib')
import ACNode
class PingNode(ACNode.ACNode):
  default_ping_interval = 0
  last_time = 0
  last_ping_time = {}

  def __init__(self):
    super().__init__()
    self.commands[ 'pong' ] = self.cmd_ack
    self.commands[ 'ack' ] = self.cmd_ack

  def parseArguments(self):
    self.parser.add('--ping','-P',default=self.default_ping_interval,action='store',type=int,
         help='Ping interval, in seconds (default is 0, Off)')

    super().parseArguments()

  def loop(self):
    if self.cnf.ping:
      if time.time() - self.last_time > self.cnf.ping:
        for node in self.last_ping_time.keys():
           self.logger.info("NO Ack from "+node)
            
        for node in self.cnf.secrets.keys():
           self.send(node, "ping")
           self.last_ping_time[node] = time.time()

        self.last_time = time.time()

    super().loop()

  def cmd_ack(self, msg):
    if self.last_ping_time.get(msg['node']):
       del self.last_ping_time[msg['node']]
    else:
       self.logger.info("Unexcepted Ack from "+msg['node'])

    self.logger.debug("Ack from "+msg['node'])
    return

# Allow this class to auto instanciate if
# we run it on its own.
#
if __name__ == "__main__":
  pingNode = PingNode()
  if not pingNode:
    sys.exit(1)
  exitcode = pingNode.run()
  sys.exit(exitcode)

