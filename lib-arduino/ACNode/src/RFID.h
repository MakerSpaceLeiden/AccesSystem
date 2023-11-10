#ifndef _H_RFID
#define _H_RFID

#include <stddef.h>
#include <functional>

#include <ACNode-private.h>
#include <ACBase.h>
#include <Wire.h>

class RFID : public ACBase {
  public:
    RFID();
    const size_t MAX_TAG_LEN = 48;
    const char * name() { return "RFID"; }
    
    void processAndRateLimitCard(unsighed char * buff, size_t len);
    void registerCallback(unsigned char irqpin);

    void begin();
    void loop();

    void report(JsonObject& report);

    typedef std::function<ACBase::cmd_result_t(const char *)> THandlerFunction_SwipeCB;

    RFID& onSwipe(THandlerFunction_SwipeCB fn) 
	{ _swipe_cb = fn; return *this; };
  
  private:
    bool _irqMode = false;
    THandlerFunction_SwipeCB _swipe_cb = NULL;
    char lasttag[MAX_TAG_LEN * 4];      // Up to a 3 digit byte and a dash or terminating \0. */
    unsigned long lastswipe, _scan, _miss;
};

#endif
