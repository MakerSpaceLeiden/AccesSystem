#!/usr/bin/env python3.4
#
import time
import sys
import os

from ACNode import ACNode

class ActuatorACNode(ACNode):

  def __init__(self):
    super().__init__()

    # An actuator only responds to approval messages..
    #
    self.commands[ 'approved' ] = self.cmd_approved

  def setup(self):
    super().setup()

    if not "execute" in dir(self):
       print("FATAL: method execute() is not defined in class {0}. Terminating. ".format(self.__class__.__name__))
       sys.exit(1)

  last_tag_shown = None
  last_tag_beat = None

  def send_request(self, command, target_node, target_machine, tag_uid):
      # Track what tags we ask approval for - so we can log it slightly nicer once
      # we get the approval back.
      self.last_tag_shown = tag_uid
      self.last_tag_beat = self.beat()

      return super().send_request(command, target_node, target_machine, tag_uid)

  def cmd_approved(self,path, node,theirbeat, msg):
    acl, cmd, machine, beat = self.split_payload(msg) or (None, None, None, None)
    if not acl:
       return

    if beat != self.last_tag_beat:
      self.logger.info("Approval pertains to an unknown request. ignoring.")
      return

    tag = 'UnknownCard'
    if self.last_tag_shown:
      tag = '-'.join(str(int(bte)) for bte in self.last_tag_shown)

    self.logger.info("Approval {} {} for {}".format(cmd, machine, tag))
    err = self.execute()

    if err:
      self.logger.info("Unexpected result; ignored")
 
    return

