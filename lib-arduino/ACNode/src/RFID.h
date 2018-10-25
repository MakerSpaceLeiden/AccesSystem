#ifndef _H_RFID
#define _H_RFID

#include <stddef.h>
#include <functional>

#include <ACNode.h>
#include <ACBase.h>
#include <MFRC522.h>

class RFID : public ACBase {
  public:
    const char * name() { return "RFID"; }
    
    RFID(const byte sspin, const byte rstpin); // Allow -1 to signal a not-connected pin.

    void begin();
    void loop();

    typedef std::function<void(const char *)> THandlerFunction_SwipeCB;

    RFID& onSwipe(THandlerFunction_SwipeCB fn) 
	{ _swipe_cb = fn; return *this; };
  
  private:
    MFRC522 _mfrc522;
    THandlerFunction_SwipeCB _swipe_cb = NULL;

    char lasttag[MAX_TAG_LEN * 4];      // Up to a 3 digit byte and a dash or terminating \0. */
    unsigned long lasttagbeat;          // Timestamp of last swipe.
};
#endif
