#ifndef _H_RFID_PN532
#define _H_RFID_PN532

#include <stddef.h>
#include <functional>

#include <Adafruit_PN532.h>
#include "RFID.h"

class RFID_PN532: public RFID {
  public:
    const char * name() { return "RFID-PN532"; }
    
    RFID(TwoWire *i2cBus, const byte i2caddr, const byte rstpin = RFID_RESET_PIN, const byte irqpin = RFID_IRQ_PIN);

    void begin();
    void loop();

    void report(JsonObject& report);

    typedef std::function<ACBase::cmd_result_t(const char *)> THandlerFunction_SwipeCB;

    RFID& onSwipe(THandlerFunction_SwipeCB fn) 
	{ _swipe_cb = fn; return *this; };
  
  private:
    Adafruit_PN532 * _pn532;
};
#endif
