#include <RFID_PN532_EX.h>
#include <Wire.h>

const uint8_t FOREVER = 255;

RFID_PN532_EX::RFID_PN532_EX()
{
   _i2cNFCDevice = new PN532_I2C(Wire);
   _nfc532 = new PN532(*_i2cNFCDevice);
}

void RFID_PN532_EX::begin() {
  Log.println("Searching for PN532_EX reader:");
  delay(100); // Seems Wire() needs to settle.

  _nfc532->begin();
  uint32_t version = _nfc532->getFirmwareVersion();
  if (!version) {
	Log.println(" FAIL\nERROR: no EX reader found.");
	return;
  };

  uint8_t chip_version = (version>>24) & 0xFF;
  uint8_t fw_version_maj = (version>>16) & 0xFF;
  uint8_t fw_version_min = (version>>8) & 0xFF;

  Log.printf("Detected PN532-EX: Chip version: %d,  Firmware vesion: %d.%d\n", chip_version, fw_version_maj,fw_version_min);

  _nfc532->SAMConfig();

  _nfc532->setPassiveActivationRetries(FOREVER);
}

bool RFID_PN532_EX::alive() { 
	return  _nfc532->getFirmwareVersion() ? true : false;
}

void RFID_PN532_EX::loop() {
    uint8_t uid[RFID_MAX_TAG_LEN];
    uint8_t uidLength; 

    if (true == _irqMode) {
        if (!cardScannedIrqSeen)
             return;
        cardScannedIrqSeen = false;
    } else {
        if (!_nfc532->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 5)) {
	  _nfc532->setPassiveActivationRetries(FOREVER);
	   return;
	};
    };

    if (!uidLength || uidLength > RFID_MAX_TAG_LEN) {
	Log.println("Missed/malformed scan");
        _miss++;
	return;
    };

    RFID::processAndRateLimitCard(uid, uidLength);
    _scan++;

    return;
}
