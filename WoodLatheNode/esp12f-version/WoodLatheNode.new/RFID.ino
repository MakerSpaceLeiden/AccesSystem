#include "RFID.h"


#include <MFRC522.h>
MFRC522 mfrc522;

void RFID::configureRFID(uint8 sspin, uint8 rstpin) {
  mfrc522 = MFRC522(sspin, rstpin);

  mfrc522.PCD_Init();   // Init MFRC522
  mfrc522.PCD_DumpVersionToSerial();
}

int RFID::handleRFID(unsigned long b, const char * rest) {

  if (!strncmp("revealtag", rest, 9)) {
    if (b < lasttagbeat) {
      Log.println("Asked to reveal a tag I no longer have a record off, ignoring.");
      return 1;
    };
    char buff[MAX_MSG];
    snprintf(buff, sizeof(buff), "lastused %s", lasttag);
    send(buff);
    return 1;
  }
  if (!strncmp("denied", rest, 6) || !strncmp("unknown", rest, 7)) {
    Log.println("Flash LEDS");
    orangeLED.setState(LED_FAST);
    delay(1000);
    orangeLED.setState(LED_OFF);
    return 1;
  };

  if (!strncmp("approved", rest, 8) || !strncmp("energize", rest, 8)) {
    machinestate = POWERED;
    return 1;
  }
  return 0;
}

void RFID::loop() {
  super().loop();

  if ( ! mfrc522.PICC_IsNewCardPresent())
    return;

  // Select one of the cards
  if ( ! mfrc522.PICC_ReadCardSerial())
    return;

  MFRC522::Uid uid = mfrc522.uid;
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
  Sha256.initHmac((const uint8_t*)passwd, strlen(passwd));
  Sha256.print(beatAsString);
  Sha256.write(uid.uidByte, uid.size);
  const char * tag_encoded = hmacToHex(Sha256.resultHmac());

  static char buff[MAX_MSG];
  snprintf(buff, sizeof(buff), "energize %s %s %s", nodename, machine, tag_encoded);
  send(buff);
}


