#include <stddef.h>
#include <functional>
#include <ArduinoJSON.h>

#include <ACBaseNode.h>
#include <ACBase.h>
#include <LED.h>

class RestAPI : public ACBase {
public:
    typedef enum { BOOT = 0,
        WAITING_FOR_NTP, /* waiting for NTP sync; continue only if we get a sane date in this century */
        FETCH_CA, /* Fetch the server/CA certificates with an insecured call*/
        REGISTER, /* we are not paired; start registering and get a challange */
        WAIT_FOR_REGISTER_SWIPE, /* wait for a swipe & and then complete challenge response to pair */
        CHECK_REGISTRATION, /* when we are paired - check if the registration is still valid */
        FULLY_REGISTERED, /* sit authenticated - do nothing */
        RETRYABLE_FAIL, /* retry something a few seconds later */
        WIFI_FAIL_REBOOT /* reboot; then retry from scratch */
    } state_t;

    void begin();
    void loop();
    bool ready() { return FULLY_REGISTERED == md; };
    ACBase::cmd_result_t handleTagSwipe(const char * tag);
    
    state_t state() { return md; };
    
    JsonDocument rest(const char *url);
    String stationname() { return _stationName; }
    // const char * stationname() { return _stationName.c_str(); }
    void setTerminalname(const char *name) { _terminalName = name; };

private:
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
