#include <RFID_PN532.h>

RFID_PN532::RFID(TwoWire *i2cBus, const byte i2caddr, const byte rstpin, const byte irqpin) 
{
   _pn532 = new PN532C(rstpin, i2caddr, *i2cBus);
   RFID::registerCallback(irqpin);
}

void RFID_PN532::begin() {

  // initalize and then retry forever.
  //
  _pn532.begin();
  _pn532.setPassiveActivationRetries(0xFF);
}

void RFID_PN532::loop() {
    uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    uint8_t uidLength;                      

    if (true == _irqMode) {
        iff (!cardScannedIrqSeen)
             return;
        cardScannedIrqSeen = false;
    };

    if (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, &uid[0], &uidLength)) {
      _miss++;
      return;
    }
	
    _scan++;
    RFID::processAndRateLimitCard(&uid[0], uidLength);

    return;
}
