#!/usr/bin/env python3.4
#
import time 
import sys
import os

sys.path.append('../lib')
from SensorACNode import SensorACNode
from OfflineModeACNode import OfflineModeACNode

class ReaderOnlyNode(SensorACNode,OfflineModeACNode):
  command = "open"

  # We load the hardware related libraries late and
  # on demand; this allows for an '--offline' flag.
  #
  def setup(self):
    super().setup()

    if self.cnf.offline:
       self.logger.info("TEST: import MFRC522")
    else:
       # Note: The current MFC522 library claims pin22/GPIO25
       # as the reset pin -- set by the constant NRSTPD near
       # the start of the file.
       #
       import MFRC522
       MIFAREReader = MFRC522.MFRC522()

  last_tag = None

  def loop(self):
   super().loop()

   uid = None
   if self.cnf.offline:
     (status,TagType) = (None, None)
   else:
     (status,TagType) = MIFAREReader.MFRC522_Request(MIFAREReader.PICC_REQIDL)
     if status == MIFAREReader.MI_OK:
        (status,uid) = MIFAREReader.MFRC522_Anticoll()
        if status == MIFAREReader.MI_OK:
          logger.info("Swiped card "+'-'.join(map(str,uid)))
        else:
          uid = None
     
   if self.last_tag != uid:
      localtime = time.asctime( time.localtime(time.time()) )
      logger.info(localtime + "     Card UID: "+'-'.join(map(str,uid)))

      self.send_request(uid)
      self.last_tag = uid

# Spin up a node; and run it forever; or until aborted; and
# provide a non-zero exit code as/if needd.
#
#
acnode = ReaderOnlyNode()

if not acnode:
  sys.exit(1)

exitcode = acnode.run()

sys.exit(exitcode)

