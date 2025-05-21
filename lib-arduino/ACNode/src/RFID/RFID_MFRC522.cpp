#include <RFID/RFID_MFRC522.h>
#include <MFRC522.h>

// #define RFID_CHECK_INTERVAL (60 * 60 * 1000)
// #define RFID_RESET_INTERVAL (3600 * 1000)

// if we are in IRQ mode; and we've seen no card; then just
// periodically re-arm the reader. Every few seconds seems to
// be enough.
//
// XXX find datasheet and put in corect time.
//
#define RFID_REPRIME_IN_IRQ_MODE (500)

// https://www.nxp.com/docs/en/data-sheet/MFRC522.pdf

RFID_MFRC522::RFID_MFRC522(const byte sspin , const byte rstpin , const byte irqpin , const byte spiclk , const byte spimiso , const byte spimosi )
{
#ifdef ESP32
    if (spiclk != 255 || spimiso != 255 || spimosi != 255)
        SPI.begin(spiclk, spimiso, spimosi);
    else
#endif
        SPI.begin();
    
    _spiDevice = new MFRC522_SPI(sspin, rstpin, &SPI);
    _mfrc522 = new MFRC522(_spiDevice);
    _irqpin = irqpin;
    _rstpin = rstpin;
    
    Debug.println("MFRC522: SPI wired.");
}

RFID_MFRC522::~RFID_MFRC522() {
    Log.println("RFID_MFRC522 does not support a destroy yet.");
    // delete _mfrc522;
    // delete _spiDevice;
}

RFID_MFRC522::RFID_MFRC522(TwoWire *i2cBus, const byte i2caddr, const byte rstpin, const byte irqpin)
{
    _i2cDevice = new MFRC522_I2C(rstpin, i2caddr, *i2cBus);
    _mfrc522 = new MFRC522(_i2cDevice);
    _irqpin = irqpin;
    _rstpin = rstpin;
    
    Debug.println("MFRC522: I2C wired.");
}

void RFID_MFRC522::clearInt() {
    _mfrc522->PCD_WriteRegister(_mfrc522->ComIrqReg, 0x7F);
    _mfrc522->PICC_HaltA();
};

void RFID_MFRC522::activateScanning() {
    _mfrc522->PCD_WriteRegister(_mfrc522->FIFODataReg, _mfrc522->PICC_CMD_REQA);
    _mfrc522->PCD_WriteRegister(_mfrc522->CommandReg, _mfrc522->PCD_Transceive);
    _mfrc522->PCD_WriteRegister(_mfrc522->BitFramingReg, 0x87); // start data transmission, last byte bits, 9.3.1.14
};

void RFID_MFRC522::begin() {
    reset();

    Debug.printf("MFRC522: Firmware %s\n",firmwareVersionString().c_str());
    
    if (_irqpin != 255) {
        /* Set1 | RxIrq (table 30, page 40) -- IRQ on read completed */
        _mfrc522->PCD_WriteRegister(_mfrc522->ComIEnReg, 0xA0);
        RFID::registerCallback(_irqpin);
        delay(100);
        clearInt();
        activateScanning();
        cardScannedIrqSeen = false;
        Debug.printf("MFRC522: Now scanning in IRQ mode (pin %d)\n", _irqpin);
    } else {
       Debug.println("MFRC522: Now scanning in Polling mode");
    };
   
#if 0 
    // Note: this seems to wedge certain cards.
    if (_debug)
        _mfrc522->PCD_DumpVersionToSerial();
#endif
}

void RFID_MFRC522::reset() {
    if (_rstpin != 255) {
        xdigitalWrite(_rstpin,LOW);
        Debug.printf("MFRC522: Reset (HW) and (re)init (pin %d)\n", _rstpin);
    } else {
        _mfrc522->PCD_Reset();
        Debug.println("MFRC522: Reset (Soft) and (re)init");
    }
    delay(50); // minimal 35 mS
    _mfrc522->PCD_Init();     // Init MFRC522
    delay(15);
}

void RFID_MFRC522::loop() {
    if ((_irqMode && cardScannedIrqSeen) || (!_irqMode && _mfrc522->PICC_IsNewCardPresent())) {
        if (_mfrc522->PICC_ReadCardSerial() &&  _mfrc522->uid.size) {
#if 0
	    Debug.printf("uid size  :%lu\n", _mfrc522->uid.size);
	    Debug.printf("uid ptr   :%p\n", _mfrc522->uid.uidByte);
	    Debug.printf("uid str   :");
            for(size_t i = 0; i <  _mfrc522->uid.size; i++)
		    Debug.printf("%s%03d",  i ? "-" : "", _mfrc522->uid.uidByte[i]);
	    Debug.printf("\n");
#endif
            processAndRateLimitCard(_mfrc522->uid.uidByte,_mfrc522->uid.size);
            _scan++;
        } else {
            _miss++;
        };

        // Stop the reading.
        _mfrc522->PICC_HaltA();

        // clear the interupt and re-arm the reader.
        if (_irqMode) {
            clearInt();
            cardScannedIrqSeen = false;
        };
        return;
    };
    
#ifdef RFID_REPRIME_IN_IRQ_MODE
    if (_irqMode &&(millis() - _lastActivate > RFID_REPRIME_IN_IRQ_MODE)) {
        _lastActivate = millis();
        activateScanning();
        return;
    };
#endif
    
#ifdef RFID_RESET_INTERVAL
    if (millis() - _lastReset > RFID_RESET_INTERVAL) {
        Debug.printf("Courtesy reset of RFID\n", version);
        _lastReset = millis();
        reset();
        return;
    };
#endif
    
#ifdef RFID_CHECK_INTERVAL
    if (millis() - lastswipe> RFID_CHECK_INTERVAL && millis() - _lastI2Ccheck > RFID_CHECK_INTERVAL) {
        _lastI2Ccheck = millis();
        byte version = _mfrc522->PCD_ReadRegister(MFRC522::VersionReg);
        if (version < 0x80 && version > 0x100) {
            Log.printf("Alert - RFID reader gave an odd response (%x) - resetting\n", version);
            reset();
	    rfid_vfail++;
        }
        else {
            if (!_mfrc522->PCD_PerformSelfTest()) {
                Log.printf("Alert - RFID reader failed the self test - resetting\n");
	    	rfid_tfail++;
            } else {
                Debug.printf("RFID reader passed selftest ok\n");
    		rfid_tests++;
            };
            reset();
        };
        return;
    };
#endif
    return;
}

void RFID_MFRC522::report(JsonObject& report) {
	report["mfrc522_failed_version_tests"] = rfid_vfail;
	report["mfrc522_failed_self_tests"] = rfid_tfail;
	report["mfrc522_ok_self_tests"] = rfid_tests;
};

String RFID_MFRC522::firmwareVersionString() {
        char * str;
	unsigned char version = _mfrc522->PCD_ReadRegister(MFRC522::VersionReg);
        switch(version) {
		case 0x00: str="00-i2c-error"; break;
		case 0xFF: str="FF-i2c-error"; break;
                case 0x90: str="v0.0"; break;
                case 0x91: str="v1.0"; break;
                case 0x92: str="v2.0"; break;
                case 0x12: str="fake"; break;
                case 0x88: str="clne"; break;
                default:   str="unkn"; break;
        };
        return String("") + String(str) + String(" (0x") + String(version,HEX) + String(")");
};

String RFID_MFRC522::stateString() { 
	String res = _mfrc522->PCD_PerformSelfTest() ? "pass" : "FAIL";
        begin();
   	return res;
}
