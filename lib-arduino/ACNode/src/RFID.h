#ifndef _H_RFID
#define _H_RFID

#include <stddef.h>
#include <functional>

#include <ACNode-private.h>
#include <ACBase.h>
#include <Wire.h>

// global variable for IRQ handler.
extern volatile bool cardScannedIrqSeen;

class RFID : public ACBase {
  public:
    void processAndRateLimitCard(unsigned char * buff, size_t len);
    void registerCallback(unsigned char irqpin);

    void report(JsonObject& report);

    typedef std::function<ACBase::cmd_result_t(const char *)> THandlerFunction_SwipeCB;

    RFID& onSwipe(THandlerFunction_SwipeCB fn) 
	{ _swipe_cb = fn; return *this; };
  
  protected:
    bool _irqMode = false;
    THandlerFunction_SwipeCB _swipe_cb = NULL;
    char lasttag[RFID_MAX_TAG_LEN * 4];      // Up to a 3 digit byte and a dash or terminating \0. */
    unsigned long lastswipe, _scan, _miss;
};

#endif
