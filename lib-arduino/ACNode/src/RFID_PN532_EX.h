#ifndef _H_RFID_PN532_EX
#define _H_RFID_PN532_EX

#include <stddef.h>
#include <functional>
#include <PN532.h>
#include <PN532_I2C.h>

#include "RFID.h"

class RFID_PN532_EX: public RFID {
  public:
    RFID_PN532_EX();

    const char * name() { return "NFC-EX"; };

    void begin() ;
    void loop();

  private:
    PN532_I2C * _i2cNFCDevice;
    PN532 * _nfc532;
};
#endif
