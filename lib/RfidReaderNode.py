#!/usr/bin/env python3.4
#
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
       MIFAREReader = MFRC522.MFRC522()

  def readtag(self):
    uid = super().readtag()
    if uid:
      return uid

    if self.cnf.offline:
      return None

    (status,TagType) = MIFAREReader.MFRC522_Request(MIFAREReader.PICC_REQIDL)
    if status != MIFAREReader.MI_OK:
      return None

    (status,uid) = MIFAREReader.MFRC522_Anticoll()
    if status != MIFAREReader.MI_OK:
      return None

    tag = '-'.join(map(str,uid))
    self.logger.info("Detected card: " + tag)

    return  uid
