#ifndef _H_RFID_PN532
#define _H_RFID_PN532

#include <stddef.h>
#include <functional>

#include <Adafruit_PN532.h>
#include "RFID.h"

class RFID_PN532: public RFID {
  public:
    RFID_PN532(TwoWire *i2cBus, const byte i2caddr, const byte rstpin = RFID_RESET_PIN, const byte irqpin = RFID_IRQ_PIN);
//    ~RFID_PN532();

    const char * name() { return _name; };

    void begin() ;
    void loop();

  private:
    Adafruit_PN532 * _pn532;
    const char PN352_NAME_FORMAT[32] =  "RFID-PN532-%d-v%d.%d"; // %d are unsigned bytes
    char _name[32] = "RFID-PN532-XXX-vXXX.XXX"; // extra room for version number
};
#endif
