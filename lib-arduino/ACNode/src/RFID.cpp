#include <RFID.h>


RFID::RFID(const byte  sspin = 255, const byte  rstpin = 255)  : _mfrc522(sspin, rstpin)
{
  SPI.begin();             // Init SPI bus
  _mfrc522.PCD_Init();     // Init MFRC522
    
    // Todo - move to IRQ land.
}

void RFID::begin() {
    if (_debug)
        _mfrc522.PCD_DumpVersionToSerial();
}

void RFID::loop() {
    if ( ! _mfrc522.PICC_IsNewCardPresent())
        return;
    
    if ( ! _mfrc522.PICC_ReadCardSerial())
        return;

    if (_mfrc522.uid.size == 0)
        return;

    
    lasttag[0] = 0;
    for (int i = 0; i < _mfrc522.uid.size; i++) {
        char buff[5];
        snprintf(buff, sizeof(buff), "%s%d", i ? "-" : "", _mfrc522.uid.uidByte[i]);
        strcat(lasttag, buff);
    }
    // lasttagbeat = beatCounter;

    char tag_encoded[MAX_MSG];
    strncpy(tag_encoded, lasttag, MAX_MSG);

    if (_acnode->cloak(tag_encoded)) {
        Log.println("Tag could not be encoded. giving up.");
        return;
    }
    
    static char buff[MAX_MSG];
    snprintf(buff, sizeof(buff), "energize %s %s %s", moi, machine, tag_encoded);
    send(NULL, buff);
    
    _swipe_cb(lasttag); 
    return;
}


