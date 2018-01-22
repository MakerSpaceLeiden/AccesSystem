#!/usr/bin/env python3.4

import os
import sys
import time
import hmac
import hashlib

sys.path.append('../lib')
import ACNode as ACNode

class Test(ACNode.ACNode):
  weAreReady = False

  def parseArguments(self):
    self.parser.add('--destination', required=True,
         help='Destination of Payload to send')
    self.parser.add('--payload', nargs='*', default = 'ping', 
         help='Payload to send')

    super().parseArguments()

  def secret(self, node = None):
    return self.cnf.secret

  def on_subscribe(self, client, userdata, mid, granted_qos):
    super().on_subscribe(client, userdata, mid, granted_qos)
    self.weAreReady = True

  def loop(self):
    super().loop()
    if self.weAreReady:
      self.send(self.cnf.destination, ' '.join(self.cnf.payload))
      self.forever = 0

test = Test()

if not test:
  sys.exit(1)

exitcode = test.run()
sys.exit(exitcode)



