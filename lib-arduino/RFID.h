#include "MakerspaceMQTT.h"
#pragma once


class RFID : public ACNode {
  public:
    void configureRFID(uint8 sspin, uint8 rstpin);
    int handleRFID(unsigned long b, const char * rest);

  private:
    const char ota_password[32];
    char lasttag[MAX_TAG_LEN * 4];      // Up to a 3 digit byte and a dash or terminating \0. */
    unsigned long lasttagbeat;          // Timestamp of last swipe.
};

