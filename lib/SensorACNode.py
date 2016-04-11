#!/usr/bin/env python3.4
#
import time
import sys
import os

from ACNode import ACNode

class SensorACNode(ACNode):
  subscribed = None
  fake_time = 0
  command = None
  last_tag = None

  # In addition; have one extra argument - which allows us to 'fake'
  # swipe tags from the command line - and see the effect.
  #
  def parseArguments(self):
    self.parser.add('tags', nargs='*',
       help = 'Zero or more test tags to "pretend" offer with 5 seconds pause. Once the last one has been offered the daemon will enter the normal loop.')

    super().parseArguments()

  def setup(self):
    super().setup()

    if not self.command:
       print("FATAL: command is not defined in class {0}. Terminating. ".format(self.__class__.__name__),
                file=sys.stderr)
       sys.exit(1)

  def on_subscribe(self, client, userdata, mid, granted_qos):
      super().on_subscribe(client, userdata,mid,granted_qos)
      self.subscribed = True

  def send_request(self,uid = None):
      super().send_request(self.command, self.cnf.node, self.cnf.machine, uid)

  def run(self):
    self.fake_time = time.time()
    super().run()

  def loop(self):
   super().loop()

   if self.cnf.tags and time.time() - self.fake_time > 5 and self.subscribed:

     tag = self.cnf.tags.pop(0)
     self.logger.info("Pretending swipe of fake tag: <"+tag+">")

     if sys.version_info[0] < 3:
        tag_asbytes= ''.join(chr(int(x)) for x in tag.split("-"))
     else:
        tag_asbytes = bytearray(map(int,tag.split("-")))

     self.last_tag = tag_asbytes
     self.send_request(tag_asbytes)
     self.fake_time = time.time()

     if not self.cnf.tags:
        self.logger.info("And that was the last pretend tag; going into normal mode now.")

