#if 0
#ifndef _H_RFID_PN532_NFC
#define _H_RFID_PN532_NFC

#include <stddef.h>
#include <functional>
#include <DFRobot_PN532.h>

#include "RFID.h"

class RFID_PN532_NFC: public RFID {
  public:
    RFID_PN532_NFC();

    const char * name() { return "NFC"; };

    void begin() ;
    void loop();

  private:
    DFRobot_PN532_IIC * _nfc;
};
#endif
#endif
