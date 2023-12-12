#include <RFID_PN532_EX.h>
#include <Wire.h>

const uint8_t FOREVER = 255;
const int RFID_TRIES = 1;
const int RFID_TIMEOUT = 0;  // some sort of weird bus-related speed unit.

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
  _nfc532->setPassiveActivationRetries(_irqMode ? FOREVER : RFID_TRIES);
}

bool RFID_PN532_EX::alive() { 
	return  _nfc532->getFirmwareVersion() ? true : false;
}

void RFID_PN532_EX::loop() {
    uint8_t uid[RFID_MAX_TAG_LEN];
    uint8_t uidLength; 

    unsigned long s = micros();
    static unsigned long d = 0;

{
	// forever is a bit of a misnomer it seems - so rearm it every 10 seconds or so.
	static unsigned long lst = 0;
	if (millis() - lst > 10*1000) {
		lst = millis();
		Log.printf("Average read: %lu microSeconds\n", d);
	};
};


    if (true == _irqMode) {
	// forever is a bit of a misnomer it seems - so rearm it every 5 seconds or so.
	static unsigned long lst = 0;
	if (millis() - lst > 5*1000) {
		lst = millis();
		_nfc532->setPassiveActivationRetries(FOREVER);
	};
        if (!cardScannedIrqSeen)
             return;
        cardScannedIrqSeen = false;
    } else {
        if (!_nfc532->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, RFID_TIMEOUT)) {
          unsigned long t = micros() - s;
	   d = (d * 10 + t)/11;
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
