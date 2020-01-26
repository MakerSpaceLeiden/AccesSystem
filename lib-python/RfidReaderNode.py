#!/usr/bin/env python3.4
#
import time
import sys

from SensorACNode import SensorACNode
from OfflineModeACNode import OfflineModeACNode

class RfidReaderNode(SensorACNode, OfflineModeACNode):
  last_read = None
  last_seen = 0

  def setup(self):
    super().setup()
    self.last_read = None
    self.last_seen = 0

    if self.cnf.offline:
       self.logger.info("TEST: import MFRC522")
    else:
       # Note: The current MFC522 library claims pin22/GPIO25
       # as the reset pin -- set by the constant NRSTPD near
       # the start of the file.
       #
       import MFRC522
       self.MIFAREReader = MFRC522.MFRC522()

  def dumpVersion(self):
    v = self.MIFAREReader.Read_MFRC522(self.MIFAREReader.VersionReg)

    if v == 0 or v == 0xFF:
       print("Unknown version reporte d- likely a comms error")
       return

    s = "unknown"
    known = { 0x80 : 'clone', 0x90: 'v0.0', 0x91: 'v1.0', 0x92 : 'v2.0' }
    if v in known:
       s = known[ v ]

    print("MFRC522: version 0x{:x} ({})".format(v,s))

  def readtag(self):
    uid = super().readtag()
    if uid:
      return uid

    if self.cnf.offline:
      self.last_read = None
      return None

    (status,TagType) = self.MIFAREReader.MFRC522_Request(self.MIFAREReader.PICC_REQIDL)
    if status != self.MIFAREReader.MI_OK:

      if self.last_read != None and time.time() - self.last_seen > 3:
         self.logger.info("Lost card");
         self.last_read = None
         self.last_seen = time.time()

      return None

    (status,uid) = self.MIFAREReader.MFRC522_Anticoll()
    if status != self.MIFAREReader.MI_OK:
      self.logger.info("Card clash")
      self.last_read = None
      self.last_seen = time.time()
      return None

    tag = '-'.join(map(str,uid))
    if self.last_read != tag:
        self.logger.info("Detected card: " + tag)
    else:
        self.logger.debug("Still seeing card: " + tag)

    self.last_read = tag
    self.last_seen = time.time()
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
  print("---")
  acnode.dumpVersion()

  print("Scanning...")
  acnode.test()

