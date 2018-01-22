#include <MFRC522.h>
#include "RFID.h"

// Last tag swiped; as a string.
//
char lasttag[MAX_TAG_LEN * 4];      // Up to a 3 digit byte and a dash or terminating \0. */
unsigned long lasttagbeat;          // Timestamp of last swipe.
MFRC522 mfrc522;

void configureRFID(unsigned sspin, unsigned rstpin) {
  mfrc522 = MFRC522(sspin, rstpin);

  mfrc522.PCD_Init();   // Init MFRC522
  mfrc522.PCD_DumpVersionToSerial();
}

int handleRFID(unsigned long b, const char * rest) {
  if (!strncmp("revealtag", rest, 9)) {
    if (b < lasttagbeat) {
      Log.println("Asked to reveal a tag I no longer have a record off, ignoring.");
      return 1;
    };
    char buff[MAX_MSG];
    snprintf(buff, sizeof(buff), "lastused %s", lasttag);
    send(NULL, buff);
    return 1;
  }
  if (!strncmp("denied", rest, 6) || !strncmp("unknown", rest, 7)) {
    Log.println("Flash LEDS");
    setOrangeLED(LED_FAST);
    delay(1000);
    setOrangeLED(LED_OFF);
    return 1;
  };

  if (!strncmp("approved", rest, 8) || !strncmp("energize", rest, 8)) {
    machinestate = POWERED;
    return 1;
  }
  return 0;
}

int checkTagReader() {
  if ( ! mfrc522.PICC_IsNewCardPresent())
    return 0;

  // Select one of the cards
  if ( ! mfrc522.PICC_ReadCardSerial())
    return 0;

  MFRC522::Uid uid = mfrc522.uid;
  if (uid.size == 0)
    return 0;

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

  return 1;
}


