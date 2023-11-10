#include <RFID_MFRC522.h>
// https://www.nxp.com/docs/en/data-sheet/MFRC522.pdf

RFID_MFRC522::RFID(const byte sspin , const byte rstpin , const byte irqpin , const byte spiclk , const byte spimiso , const byte spimosi ) 
{
#ifdef ESP32
  if (spiclk != 255 || spimiso != 255 || spimosi != 255)
     SPI.begin(spiclk, spimiso, spimosi);
  else
#endif
     SPI.begin();

   _spiDevice = new MFRC522_SPI(sspin, rstpin, &SPI);
   _mfrc522 = new MFRC522(_spiDevice);

   RFID::registerCallback(irqpin);
}

RFID_MFRC522::RFID(TwoWire *i2cBus, const byte i2caddr, const byte rstpin, const byte irqpin) 
{
   _i2cDevice = new MFRC522_I2C(rstpin, i2caddr, *i2cBus);
   _mfrc522 = new MFRC522(_i2cDevice);

   RFID::registerCallback(irqpin);
}

void RFID_MFRC522::begin() {
  _mfrc522->PCD_Init();     // Init MFRC522

  if (true == _irqMode) {
	_mfrc522->PCD_WriteRegister(_mfrc522->ComIEnReg, 0xA0 /* irq on read */);
	cardScannedIrqSeen = false; 
	Log.println("MFRC522: IRQ mode.");
   } else {
	Log.println("MFRC522: Polling mode.");
   };

   // Note: this seems to wedge certain cards.
   if (_debug)
	_mfrc522->PCD_DumpVersionToSerial();
}

void RFID_MFRC522::loop() {
    // if we are in IRQ mode; and we've seen no card; then just
    // periodically re-arm the reader. Every few seconds seems to
    // be enough. XX find datasheet and put in corect time.
    //
    if (true == _irqMode) {
     if (false == cardScannedIrqSeen) {
	static unsigned long kick = 0;
	if (millis() - kick > 500) {
		kick = millis();
    		_mfrc522->PCD_WriteRegister(_mfrc522->FIFODataReg, _mfrc522->PICC_CMD_REQA);
		_mfrc522->PCD_WriteRegister(_mfrc522->CommandReg, _mfrc522->PCD_Transceive);
		_mfrc522->PCD_WriteRegister(_mfrc522->BitFramingReg, 0x87); // start data transmission, last byte bits, 9.3.1.14
	};
        return;
      };
    } else {
	// Polling mode does not require a re-arm; instead we check fi there is a card 'now;
       if (_mfrc522->PICC_IsNewCardPresent() == 0)
          return;
    }
    if (_mfrc522->PICC_ReadCardSerial() &&  _mfrc522->uid.size) {
       RFID::processAndRateLimitCard(mfrc522->uid.uidByte,_mfrc522->uid.size);
      _scan++;
    } else {
      _miss++;
    };

    // Stop the reading.
    _mfrc522->PICC_HaltA();

    // clear the interupt and re-arm the reader.
    if (_irqMode) {
	_mfrc522->PCD_WriteRegister(_mfrc522->ComIrqReg, 0x7F);
    	cardScannedIrqSeen = false;
    };
    return;
}
