#pragma once

#define RFID_MAX_TAG_LEN (12)
#define RFID_MAX_TAG_STRING_LEN (RFID_MAX_TAG_LEN * 4) // Up to a 3 digits and a dash and/or terminating \0. */

#include <stddef.h>
#include <functional>

#include <ACBase.h>
#include <Wire.h>

#include <Display/Deck.h>

// global variable for IRQ handler.
extern volatile bool cardScannedIrqSeen;


class RFID : public ACBase {
  public:
    virtual const char * name() { return "RFID"; };
    virtual void reset() { Debug.printf("%s reset not implemented", name()); };
    virtual bool alive() { Debug.printf("%s check not implemented", name()); return true; };

    virtual String firmwareVersionString() { return "unknown"; };
    virtual String stateString() { return "state?"; };
    
    void processAndRateLimitCard(unsigned char * buff, size_t len);
    void registerCallback(unsigned char irqpin);

    void report(JsonObject report);

    typedef std::function<ACBase::cmd_result_t(const char *)> THandlerFunction_SwipeCB;

    RFID& onSwipe(THandlerFunction_SwipeCB fn) { _swipe_cb = fn; return *this; };

  protected:
    bool _irqMode = false;
    THandlerFunction_SwipeCB _swipe_cb = NULL;
    char lasttag[RFID_MAX_TAG_STRING_LEN];      // Up to a 3 digit byte and a dash or terminating \0. */
    unsigned long lastswipe = 0, _scan = 0, _miss = 0;
};

class RFID;
class ACNodeBase;

class RfidDeck : public Deck {
public:
    RfidDeck(ACNodeBase * node, RFID * r) : Deck(node),_reader(r) {};
    virtual void render_pane(bool refresh);
private:
    RFID * _reader = NULL;
};
