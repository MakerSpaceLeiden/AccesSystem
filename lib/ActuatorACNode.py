#!/usr/bin/env python3.4
#
import time
import sys
import os

from ACNode import ACNode

class ActuatorACNode(ACNode):

  def setup(self):
    super().setup()

    if not "execute" in dir(self):
       print("FATAL: method execute() is not defined in class {0}. Terminating. ".format(self.__class__.__name__),
                file=sys.stderr)
       sys.exit(1)
 
  def on_message(self,client, userdata, message):
    payload = super().on_message(client, userdata, message)

    if not payload:
       return 1

    # We know that we have a good message - so parse
    # it in our context.
    #
    try:
      what, which, result = payload.split()
    except:
      self.logger.warning("Cannot parse payload; ignored")
      return 100

    if which != self.cnf.machine:
      self.logger.info("I am a '{0}'- ignoring '{1}; ignored".format(which,self.cnf.machine))
      return 101

    if what != self.command:
      self.logger.warning("Unexpected command '{0}' - I can only <{1}> the <{2}>; ignored".format(what,self.command,self.cnf.machine))
      return 102

    if result == 'denied':
      self.logger.info("Denied XS")
      return 103

    if result != 'approved':
      self.logger.info("Unexpected result; ignored")
      return 104

    err = self.execute()

    if err:
      self.logger.info("Unexpected result; ignored")
 
    return err

