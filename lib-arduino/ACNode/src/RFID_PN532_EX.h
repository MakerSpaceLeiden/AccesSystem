#ifndef _H_RFID_PN532_EX
#define _H_RFID_PN532_EX

#include <stddef.h>
#include <functional>

// Relies on https://github.com/Seeed-Studio/PN532
// Which is not part of the Arduino ecosystem.
// Requires PN532_I2C and PN532 to be in Arduino/libraries
//
#include <PN532.h>
#include <PN532_I2C.h>

#include "RFID.h"

class RFID_PN532_EX: public RFID {
  public:
    RFID_PN532_EX();

    const char * name() { return "NFC-EX"; };

    void begin() ;
    void loop();

    bool alive();
  private:
    PN532_I2C * _i2cNFCDevice;
    PN532 * _nfc532;
};
#endif
