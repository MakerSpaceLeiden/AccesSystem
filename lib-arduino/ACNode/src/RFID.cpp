#include <RFID.h>

volatile bool cardScannedIrqSeen = false;
static void readCard() { cardScannedIrqSeen = true; }


RFID::RFID(const byte sspin , const byte rstpin , const byte irqpin , const byte spiclk , const byte spimiso , const byte spimosi ) 
{
  if (spiclk == 255 && spimiso == 255 && spimosi == 255)
     SPI.begin();
  else
     SPI.begin(spiclk, spimiso, spimosi);

  _mfrc522 = new MFRC522(sspin, rstpin);
  assert(_mfrc522);
  
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
	if (millis() - kick > 10*1000) {
		kick = millis();
    		_mfrc522->PCD_WriteRegister(_mfrc522->FIFODataReg, _mfrc522->PICC_CMD_REQA);
		_mfrc522->PCD_WriteRegister(_mfrc522->CommandReg, _mfrc522->PCD_Transceive);
		_mfrc522->PCD_WriteRegister(_mfrc522->BitFramingReg, 0x87);	
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

	// Stop the reading.
	_mfrc522->PICC_HaltA();

       // Limit the rate of reporting. Unless it is a new tag.
       //
       if (strncmp(lasttag, tag, sizeof(lasttag)) || millis() - lastswipe > 3000) {
    	      lastswipe = millis();
	      strncpy(lasttag, tag, sizeof(tag));

	      if (!_swipe_cb || (_swipe_cb(lasttag) != ACNode::CMD_CLAIMED)) {
 	      	   // Simple approval request; default is to 'energise' the contactor on 'machine'.
		   Log.println("Requesting approval");
	           _acnode->request_approval(lasttag);
	      } else {
		   Debug.println( _swipe_cb ? "internal rq used " : "callback claimed" );
	      };
       }
    }

    // clear the interupt and re-arm the reader.
    if (_irqMode) {
	_mfrc522->PCD_WriteRegister(_mfrc522->ComIrqReg, 0x7F);
    	cardScannedIrqSeen = false;
    };

    return;
}

