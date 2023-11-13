#include <RFID_PN532.h>
#include <Wire.h>

RFID_PN532::RFID_PN532(TwoWire *i2cBus, const byte i2caddr, const byte rstpin, const byte irqpin) 
{
   if (PN532_I2C_ADDRESS != i2caddr) {
	Log.println("ERROR - hardcoded driver address for PN532 does not match settings\n");
   };

   _pn532 = new Adafruit_PN532((uint8_t)irqpin, (uint8_t)rstpin, i2cBus);
   if (irqpin != -1)
	RFID::registerCallback(irqpin);
}

// RFID_PN532::~RFID_PN532() { delete _pn532; }

void RFID_PN532::begin() {

  Log.println("Searching for PN532 reader");
  if (!_pn532->begin()) {
	Log.println("ERROR: no reader found.");
	return;
  };

  uint32_t version = _pn532->getFirmwareVersion();
  uint8_t chip_version = (version>>24) & 0xFF;
  uint8_t fw_version_maj = (version>>16) & 0xFF;
  uint8_t fw_version_min = (version>>8) & 0xFF;

  Log.printf("Detected PN532: Chip version: %d,  Firmware vesion: %d.%d\n", chip_version, fw_version_maj,fw_version_min);
  snprintf(_name,sizeof(_name),PN352_NAME_FORMAT,chip_version, fw_version_maj, fw_version_min);

  // retry forever.
  _pn532->setPassiveActivationRetries(0xFF);
}

void RFID_PN532::loop() {
    uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    uint8_t uidLength;                      

    if (true == _irqMode) {
        if (!cardScannedIrqSeen)
             return;
        cardScannedIrqSeen = false;
    };

    if (!_pn532->readPassiveTargetID(PN532_MIFARE_ISO14443A, &uid[0], &uidLength)) {
      _miss++;
      return;
    }

    RFID::processAndRateLimitCard(&uid[0], uidLength);
    _scan++;

    return;
}
