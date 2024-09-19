#include <stddef.h>
#include <functional>
#include <ArduinoJSON.h>

#include <ACBaseNode.h>
#include <ACBase.h>
#include <LED.h>

#include "Display/Deck.h"

#ifndef _H_RestAPI
#define _H_RestAPI
class RestAPI : public ACBase {
public:
    typedef enum { BOOT = 0,
        WAITING_FOR_NTP, /* waiting for NTP sync; continue only if we get a sane date in this century */
        FETCH_CA, /* Fetch the server/CA certificates with an insecured call*/
        REGISTER, /* we are not paired; start registering and get a challange */
        WAIT_FOR_REGISTER_SWIPE, /* wait for a swipe & and then complete challenge response to pair */
        CHECK_REGISTRATION, /* when we are paired - check if the registration is still valid */
        FULLY_REGISTERED, /* report all complete */
        DONE, /* sit authenticated - do nothing */
        RETRYABLE_FAIL, /* retry something a few seconds later */
        WIFI_FAIL_REBOOT /* reboot; then retry from scratch */
    } state_t;
    virtual const char *name() { return "RestAPI"; };

    void begin();
    void loop();
    bool ready() { return FULLY_REGISTERED == md; };
    ACBase::cmd_result_t handleTagSwipe(const char * tag);
    
    typedef std::function<void(void)> THandlerFunction_NotifyPair;
    typedef std::function<void(void)> THandlerFunction_NotifyPaired;

    RestAPI& onPairingRequested(THandlerFunction_NotifyPair fn) { _pair_cb = fn; return *this; };
    RestAPI& onPaired(THandlerFunction_NotifyPaired fn) { _paired_cb = fn; return *this; };

    state_t state() { return md; };
    
    JsonDocument get(const char *url);
    
    // Will return the actual number of bytes read; or a -1 on error.
    // if maxbufflenp is a pointer to a max value; this cap the number
    // of bytes read; with this value updated to the number of bytes
    // actually available (if known). If buffp points to a buffer then
    // this buffer will be used; if it points to 0; it will be pointing
    // to malloc()ed buffer that needs to be freeed. If buffp is zero
    // no data is returned.
    int get(const char *url, size_t * maxbufflenp, unsigned char ** buffp);

    String stationname() { return _stationName; }
    // const char * stationname() { return _stationName.c_str(); }
    void setTerminalname(const char *name) { _terminalName = name;  };

private:
    THandlerFunction_NotifyPair _pair_cb;
    THandlerFunction_NotifyPaired _paired_cb;

protected:
    friend class RestDeck;
    
    state_t md = BOOT;
    String _stationName;
    const char * _terminalName;
    bool paired;
    
protected:
    void setStationname(String name) { _stationName = name; };

    // Historic side effect - fetching the pricelist also sets station name; so 
    // allow this. This saves a https roundtrip during startup.
    friend class PaymentAPI;
};

class RestDeck : public Deck {
public:
    RestDeck(ACNodeBase * node, RestAPI * a) : Deck(node), _restAPI(a) {};
    virtual void render_pane(bool refresh);
private:
    RestAPI * _restAPI;
};
#endif

