#include <RFID.h>
// https://www.nxp.com/docs/en/data-sheet/MFRC522.pdf

volatile bool cardScannedIrqSeen = false;
static void readCard() { cardScannedIrqSeen = true; }

RFID::RFID(const byte sspin , const byte rstpin , const byte irqpin , const byte spiclk , const byte spimiso , const byte spimosi ) 
{
#ifdef ESP32
  if (spiclk != 255 || spimiso != 255 || spimosi != 255)
     SPI.begin(spiclk, spimiso, spimosi);
  else
#endif
     SPI.begin();
/*

/Users/dirkx/Documents/Arduino/libraries/ACNode/src/RFID.cpp: In constructor 'RFID::RFID(byte, byte, byte, byte, byte, byte)':
/Users/dirkx/Documents/Arduino/libraries/ACNode/src/RFID.cpp:15:27: error: invalid conversion from 'MFRC522_SPI*' to 'byte {aka unsigned char}' [-fpermissive]
    _mfrc522 = new MFRC522(_spiDevice);
                           ^
In file included from /Users/dirkx/Documents/Arduino/libraries/ACNode/src/RFID.h:9:0,
                 from /Users/dirkx/Documents/Arduino/libraries/ACNode/src/RFID.cpp:1:
/Users/dirkx/Documents/Arduino/libraries/rfid-org/src/MFRC522.h:347:2: note:   initializing argument 1 of 'MFRC522::MFRC522(byte)'
  MFRC522(byte resetPowerDownPin);
  ^
*/
   _spiDevice = new MFRC522_SPI(sspin, rstpin, &SPI);
   _mfrc522 = new MFRC522(_spiDevice);

  if (irqpin != 255)  {
  	pinMode(irqpin, INPUT_PULLUP);
	attachInterrupt(digitalPinToInterrupt(irqpin), readCard, FALLING);
	_irqMode = true;
   };
}

RFID::RFID(TwoWire *i2cBus, const byte i2caddr, const byte rstpin, const byte irqpin) 
{
   _i2cDevice = new MFRC522_I2C(rstpin, i2caddr, *i2cBus);
   _mfrc522 = new MFRC522(_i2cDevice);

  if (irqpin != 255)  {
  	pinMode(irqpin, INPUT_PULLUP);
	attachInterrupt(digitalPinToInterrupt(irqpin), readCard, FALLING);
	_irqMode = true;
   };
}

void RFID::begin() {
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

void RFID::loop() {
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
       char tag[MAX_TAG_LEN * 4] = { 0 };
       for (int i = 0; i < _mfrc522->uid.size; i++) {
           char buff[5];
           snprintf(buff, sizeof(buff), "%s%d", i ? "-" : "", _mfrc522->uid.uidByte[i]);
           strncat(tag, buff, sizeof(tag));
       };


       // Limit the rate of reporting. Unless it is a new tag.
       //
       if (strncmp(lasttag, tag, sizeof(lasttag)) || millis() - lastswipe > 3000) {
    	      lastswipe = millis();
	      strncpy(lasttag, tag, sizeof(lasttag));

	      if (!_swipe_cb || (_swipe_cb(lasttag) != ACNode::CMD_CLAIMED)) {
 	      	   // Simple approval request; default is to 'energise' the contactor on 'machine'.
		   Log.println("Requesting approval");
	           _acnode->request_approval(lasttag);
	      } else {
		   Debug.println( _swipe_cb ? "internal rq used " : "callback claimed" );
	      };
       };
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

void RFID::report(JsonObject& report) {
	report["rfid_scans"] = _scan;
	report["rfid_misses"] = _miss;
}

