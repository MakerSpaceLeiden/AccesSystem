#!/usr/bin/env python3.4

import os
import sys
import time
import hmac
import hashlib

sys.path.append('../lib')
import ACNode as ACNode

class DrumbeatNode(ACNode.ACNode):
  default_interval = 60
  default_node = "drumbeat"

  def parseArguments(self):
    self.parser.add('--interval','-i',default=self.default_interval,action='store',type=int,
         help='DrumbeatNode interval, in seconds (default: '+str(self.default_interval)+' seconds)'),

    super().parseArguments()

  def cmd_announce(self,path,node,theirbeat,payload):
    if node != self.cnf.node:
       self.logger.info("Node '{}' (re)subscrubed; sending beat.".format(node))
       self.send(node,"beat")
    else:
       self.logger.info("Ignoring my own announce message.")
    return None

  def on_message(self,client, userdata, message):
    path, moi, node = self.parse_topic(message.topic)

    if not path:
       return None

    payload = super().on_message(client, userdata, message)
    if not payload:
       return

    try:
      dstnode, payload = payload.split(' ',1)
    except:
      self.logger.info("Could not parse '{0}' -- ignored".format(payload))
      return


  last_time = 0
  def loop(self):
    if time.time() - self.last_time > self.cnf.interval:
      self.last_time = time.time()
      self.send(self.cnf.node, "beat")

    super().loop()

if __name__ == "__main__":
  drumbeat = DrumbeatNode()
  if not drumbeat:
    sys.exit(1)
  exitcode = drumbeat.run()
  sys.exit(exitcode)

