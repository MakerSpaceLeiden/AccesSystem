#include <RFID.h>

RFID::RFID(const byte  sspin = 255, const byte  rstpin = 255)  : _mfrc522(sspin, rstpin)
{
  SPI.begin(); // Init SPI bus
  _mfrc522.PCD_Init();   // Init MFRC522
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

  MFRC522::Uid uid = _mfrc522.uid;
  if (uid.size == 0)
    return;

  lasttag[0] = 0;
  for (int i = 0; i < uid.size; i++) {
    char buff[5];
    snprintf(buff, sizeof(buff), "%s%d", i ? "-" : "", uid.uidByte[i]);
    strcat(lasttag, buff);
  }
  lasttagbeat = beatCounter;

  char beatAsString[ MAX_BEAT ];
  snprintf(beatAsString, sizeof(beatAsString), BEATFORMAT, beatCounter);

  SHA256 sha256;
  sha256.reset();
  sha256.update((unsigned char*)&beatAsString, strlen(beatAsString));
  sha256.update(uid.uidByte, uid.size);

  unsigned char binresult[sha256.hashSize()];
  sha256.finalizeHMAC(sessionkey, sizeof(sessionkey), binresult, sizeof(binresult));

  const char * tag_encoded = hmacToHex(binresult);

  static char buff[MAX_MSG];
  snprintf(buff, sizeof(buff), "energize %s %s %s", moi, machine, tag_encoded);
  send(NULL, buff);

  _swipe_cb(lasttag); 
  return;
}


