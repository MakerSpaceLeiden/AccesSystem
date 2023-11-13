#include <RFID_PN532_NFC.h>
#include <Wire.h>


RFID_PN532_NFC::RFID_PN532_NFC()
{
  const int PN532_POLLING = 0; 
  const int PN532_INTERRUPT = 1;
  _nfc = new DFRobot_PN532_IIC(-1, PN532_POLLING); 
}

void RFID_PN532_NFC::begin() {
  Log.println("Searching for PN532_NFC reader:");
  // Wire.begin();
  delay(100);

  if (!_nfc->begin()) {
	Log.println(" FAIL\nERROR: no NFC reader found.");
	return;
  };
  Log.println(" Reader found");
}

void RFID_PN532_NFC::loop() {
    if (true == _irqMode) {
        if (!cardScannedIrqSeen)
             return;
        cardScannedIrqSeen = false;
    } else {
        if (!_nfc->scan()) 
	   return;
    }

    DFRobot_PN532::sCard_t NFCcard = _nfc->getInformation();

    if (!NFCcard.uidlenght || NFCcard.uidlenght>RFID_MAX_TAG_LEN) {
	Log.println("Missed/malformed scan");
        _miss++;
	return;
    };
    RFID::processAndRateLimitCard(NFCcard.uid, NFCcard.uidlenght);
    _scan++;

    return;
}
