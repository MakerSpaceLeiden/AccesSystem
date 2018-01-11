#!/usr/bin/env python3.4

import os
import sys
import time
import hmac
import hashlib

sys.path.append('../lib')
import ACNode as ACNode

class PingPongNode(ACNode.ACNode):
  default_ping_interval = 600
  ping_interval = default_ping_interval
  last_ping_time = 0
  last_send = 0

  def __init__(self):
    super().__init__()
    self.commands[ 'ping' ] = self.cmd_ping
    self.commands[ 'pong' ] = self.cmd_pong

  def parseArguments(self):
    self.parser.add('--ping_interval','-i',default=self.default_ping_interval,action='store',type=int,
         help='MQTT alive ping/pong check interval, in seconds (default: '+str(self.default_ping_interval)+' seconds)'),

    super().parseArguments()

  def cmd_ping(self,path,node,theirbeat,payload):
    self.logger.debug("Node {} sends a ping on {} - responding with pong".format(node,self.cnf.node, path))
    self.send(node,"pong")
    return

  def on_connect(self, client, userdata, flags, rc):
    super().on_connect(client, userdata, flags, rc)

    if self.cnf.node != self.cnf.master:
      self.subscribe(client,self.cnf.node + "/" + self.cnf.node)

  def cmd_pong(self,path,node,theirbeat,payload):
    if node == self.cnf.node:
       self.last_ping_time = 0
       self.logger.info("Received our ack pong - mqtt alive.")
       return

    self.logger.info("Node {} send us an pong on {}".format(node,self.cnf.node, path))
    return

  last_ping_time = 0
  def loop(self):
    if self.last_ping_time != 0 and time.time() - self.last_ping_time > 10:
        self.logger.warning("Nor received an ACK within the usual few seconds. Is MQTT down ?")

    if self.last_ping_time and time.time() - self.last_ping_time > 300:
        self.logger.critical("MQTT down - reconnecting")
        self.reconnect()
        self.last_ping_time = 0

    if time.time() - self.last_ping_time > self.cnf.ping_interval and time.time() - self.last_send >  self.cnf.ping_interval / 20:
      if self.last_ping_time == 0:
          self.last_ping_time = time.time()
      self.last_send = time.time()
      self.send(self.cnf.node, "ping")

    super().loop()

# Allow this class to auto instanciate if
# we run it on its own.
#
if __name__ == "__main__":
  moi = PingPongNode()
  if not moi:
    sys.exit(1)
  exitcode = moi.run()
  sys.exit(exitcode)

