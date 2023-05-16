#!/usr/bin/env python3.4
#
import time
import sys
import os

from ACNode import ACNode
from ACNodeBase import ACNodeBase

class SensorACNode(ACNode):
  subscribed = None
  fake_time = 0
  command = None
  last_tag = None

  def __init__(self):
    super().__init__()
    self.commands[ 'revealtag' ] = self.cmd_revealtag
    self.commands[ 'denied' ] = self.cmd_denied
    self.commands[ 'unknown' ] = self.cmd_unknown

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
       print("FATAL: command is not defined in class {0}. Terminating. ".format(self.__class__.__name__))
       sys.exit(1)

    # Go very verbose if we are in fake est-tag mode.
    #
    if self.cnf.tags:
       self.cnf.verbose = 10

  def send_request(self, command, target_node, target_machine, tag_uid):
      self.last_tag = tag_uid
      return super().send_request(command, target_node, target_machine, tag_uid)

  def cmd_revealtag(self,msg):
    if not self.last_tag:
       self.logger.info("Asked to reveal a tag - but nothing swiped.")
       return

    tag = '-'.join(str(int(bte)) for bte in self.last_tag)

    self.logger.info("Reporting last-tag swiped at {}: {}".format(self.cnf.node, tag))
    self.send(self.cnf.master,"lastused " + tag)

  def cmd_denied(self,msg):
    acl, cmd , machine = self.split_payload(msg) or (None, None, None)
    if not cmd:
       return

    self.logger.info("Tag denied at station {} for {}".format(node,machine))
    return

  def cmd_unknown(self,msg):
    acl, cmd , machine = self.split_payload(msg) or (None, None, None )
    if not cmd:
       return

    self.logger.info("Unknown tag offered at {}: {}".format(node,machine))
    return

  def on_subscribe(self, client, userdata, mid, granted_qos):
      super().on_subscribe(client, userdata,mid,granted_qos)
      self.subscribed = True

  def readtag(self):
   if not self.cnf.tags:
     return None
   if not self.subscribed:
     return None
   if time.time() - self.fake_time < 0.5:
     return None

   tag = self.cnf.tags.pop(0)
   self.logger.info("Pretending swipe of fake tag: <"+tag+">")

   if sys.version_info[0] < 3:
        uid = ''.join(chr(int(x)) for x in tag.split("-"))
   else:
        uid = bytearray(map(int,tag.split("-")))

   if not self.cnf.tags:
         self.logger.info("And that was the last pretend tag; going into normal mode after this.")

   self.fake_time = time.time()
   return uid

