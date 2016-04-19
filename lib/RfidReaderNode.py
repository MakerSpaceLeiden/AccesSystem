#!/usr/bin/env python3.4
#
import sys

from SensorACNode import SensorACNode
from OfflineModeACNode import OfflineModeACNode

class RfidReaderNode(SensorACNode, OfflineModeACNode):

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
       self.MIFAREReader = MFRC522.MFRC522()

  def readtag(self):
    uid = super().readtag()
    if uid:
      return uid

    if self.cnf.offline:
      return None

    (status,TagType) = self.MIFAREReader.MFRC522_Request(self.MIFAREReader.PICC_REQIDL)
    if status != self.MIFAREReader.MI_OK:
      return None

    (status,uid) = self.MIFAREReader.MFRC522_Anticoll()
    if status != self.MIFAREReader.MI_OK:
      print("col")
      return None

    tag = '-'.join(map(str,uid))
    self.logger.info("Detected card: " + tag)

    return  uid

class Test(RfidReaderNode):
  command = "xx"

  def test(self):
    self.forever = 1

    while(self.forever):
      uid = self.readtag()

if __name__ == "__main__":
  acnode = Test()
  if not acnode:
    sys.exit(1)

  acnode.parseArguments()
  acnode.setup()
  acnode.subscribed = 1

  acnode.test()

