#!/usr/bin/env python3.4
#
import time 
import sys
import os

sys.path.append('../lib')
from RfidReaderNode import RfidReaderNode

class ReaderOnlyNode(RfidReaderNode):
  command = "open"
  default_target = "acnode"

  def parseArguments(self):
    self.parser.add('--target','-T',default=self.default_target,
      help='Machine (default :'+self.default_target+')'),

    super().parseArguments()

  def loop(self):
   super().loop()

   uid = self.readtag()
   if uid and self.last_tag != uid:
      localtime = time.asctime( time.localtime(time.time()) )
      self.logger.info(localtime + "     Card UID: "+'-'.join(map(str,uid)))

      print("open {}@{}".format(self.cnf.target,self.cnf.machine))
      self.send_request('open', self.cnf.target, self.cnf.machine, uid)

# Spin up a node; and run it forever; or until aborted; and
# provide a non-zero exit code as/if needd.
#
#
acnode = ReaderOnlyNode()

if not acnode:
  sys.exit(1)

exitcode = acnode.run()

sys.exit(exitcode)
