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
  default_ping_timeout = 2
  last_time = 0
  last_time_pingtimeout = 0
  last_ping_time = {}

  def __init__(self):
    super().__init__()
    self.commands[ 'ping' ] = self.cmd_ping
    self.commands[ 'pong' ] = self.cmd_ack
    self.commands[ 'ack' ] = self.cmd_ack

  def parseArguments(self):
    self.parser.add('--ping','-P',default=self.default_ping_interval,action='store',type=int,
         help='Ping interval, in seconds (default is 0, Off)')
    self.parser.add('--pingtimeout','-T',default=self.default_ping_timeout,action='store',type=int,
         help='Ping timeout, in seconds (default is '+str(self.default_ping_timeout)+')')

    super().parseArguments()

  def loop(self):
    if self.cnf.ping:
      if time.time() - self.last_time_pingtimeout > self.cnf.pingtimeout:
        for node in self.last_ping_time.keys():
           self.logger.info("Timeout on ping-ack from "+node)
        self.last_time_pingtimeout = time.time() + self.cnf.ping * 100
            
      if time.time() - self.last_time > self.cnf.ping:
        for node in self.cnf.secrets.keys():
           self.send(node, "ping")
           self.last_ping_time[node] = time.time()
        self.last_time = time.time()
        self.last_time_pingtimeout = time.time()

    super().loop()

  def cmd_ping(self, msg):
    self.send(msg['node'],"pong")
    self.logger.debug("Ping-reply to "+msg['node'])
    return

  def cmd_ack(self, msg):
    if self.last_ping_time.get(msg['node']):
       if time.time() - self.last_ping_time[msg['node']] > self.cnf.pingtimeout:
          self.logger.info("Late ping-ack from "+msg['node'])
       del self.last_ping_time[msg['node']]
    else:
       self.logger.info("Unexpected ping-ack from "+msg['node'])

    self.logger.debug("Ping-ack from "+msg['node'])
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

