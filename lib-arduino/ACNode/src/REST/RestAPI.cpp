#include "REST/RestAPI.h"
#include "selfsign.h"
#include "rest.h"
#include "util/common-utils.h"

void RestAPI::begin() {
    md = WAITING_FOR_NTP;
    paired = false;
    
    configTime(0, 0, NTP_SERVER);
    
    switch(setupAuth(_terminalName)) {
        case NOERROR_OK:
            Debug.println("Paired; can continue without a network if need be.");
            paired = true;
            break;
        case NOERROR:
            Log.println("Not paired - we will need a network.");
            break;
        case ERR_RETRYABLE:
        case ERR_FATAL:
            md = WIFI_FAIL_REBOOT;
            break;
    };
}

ACBase::cmd_result_t RestAPI::handleTagSwipe(const char * tag) {
    rest_ret_t ret;

    if (md != WAIT_FOR_REGISTER_SWIPE)
        return ACBase::CMD_DECLINE;
    
    Log.println("Admin Tag swipe");
    if ((ret = registerDeviceSwipe(_terminalName, tag)) == NOERROR) {
        paired = true;
        md = FULLY_REGISTERED;
    };
    Log.println("Tag swipe handled");
    return ACBase::CMD_CLAIMED;
}

int RestAPI::get(const char *url, size_t * maxbufflenp, unsigned char ** buffp, String encodedpostargs) {
    unsigned char *p = NULL;
    if (buffp) p = *buffp;
    rest_ret_t ret;

    size_t n = raw_rest(_terminalName,url,maxbufflenp,buffp,&ret,encodedpostargs);

    switch(ret) {
        case NOERROR_OK:
        case NOERROR:
            return n;
            break;
        case ERR_FATAL:
            if (md < FULLY_REGISTERED) md = WIFI_FAIL_REBOOT;
            break;
        case ERR_REPAIR:
            paired = false;
            md = WAITING_FOR_NTP;
            break;
        case RETRYABLE_FAIL:
            md = WAITING_FOR_NTP;
            break;
    }
    if (p == NULL && *buffp)
        free(*buffp);
    return -1;
}


JsonDocument RestAPI::get(const char *url,String encodedpostargs) {
    rest_ret_t ret;

    JsonDocument out = raw_rest(_terminalName, url, &ret,encodedpostargs);
    switch(ret) {
        case NOERROR_OK:
        case NOERROR:
            return out;
            break;
        case ERR_FATAL:
            if (md < FULLY_REGISTERED) md = WIFI_FAIL_REBOOT;
            break;
        case ERR_REPAIR:
            paired = false;
            md = WAITING_FOR_NTP;
            break;
        case RETRYABLE_FAIL:
            md = WAITING_FOR_NTP;
            break;
    }
    
    JsonDocument emptyDoc;
    return emptyDoc;
}

void RestAPI::sentNotification(String sender, String dest, String subject, String msg) {
    String payload = encodeargs({
        "from", sender.length() ? sender : _terminalName,
        "to", dest,
        "subject", subject,
        "msg", msg
    },false);

    rest_ret_t ret;
    raw_rest(_terminalName,TERMINAL_URL NOTIFY_API_PATH,NULL,NULL,&ret,payload);
    
    if (ret != NOERROR_OK) {
        Log.printf("Sending of message %s to %s failed\n", subject.c_str(), dest.c_str());
    } else {
        Debug.printf("Message %s to %s sent\n", subject.c_str(), dest.c_str());
    };
    // it is best effort - so we're not reporting any errors; nor are we queueing
    // things in case of error.
}

void RestAPI::loop()
{
    rest_ret_t ret = NOERROR;

    // shortcicuit once we're completely done. Reregistering needs a reset/active state change.
    if (md == DONE)
        return;
    
    static unsigned long lst = millis(), freezeout = 0;
    const unsigned long MAX_FREEZEOUT = 5 * 60 * 1000;
    if (freezeout > MAX_FREEZEOUT)
        freezeout = MAX_FREEZEOUT;

    if (freezeout && (millis() - lst < freezeout))
        return;

    lst = millis();
        
    if (!eth_connected()) {
        Debug.println("RestAPI: not connected - retry later");
        ret = ERR_RETRYABLE;
        freezeout = (freezeout + 1 * 1000) *2;
        return;
    };
    
    switch (md) {
        case BOOT:
            break;
        case WAITING_FOR_NTP:
            // display.showString("ntp");
            if (time(nullptr) > 3600)
                md = FETCH_CA;
            break;
        case FETCH_CA:
            // display.showString("F CA");
            if ((ret = fetchCA(_terminalName)) == NOERROR) {
                md = paired ? CHECK_REGISTRATION : REGISTER;
            }
            break;
        case CHECK_REGISTRATION:
        {
            // display.showString("check");
            JsonDocument out = raw_rest(_terminalName, TERMINAL_URL REGISTER_PATH, &ret);
            if (ret == NOERROR_OK) {
                Debug.println("Registered & paired up ok");
                md = FULLY_REGISTERED;
            };
        };
            break;
        case REGISTER:
            // display.showString("reg");
            ret = registerDevice(_terminalName);
            switch(ret) {
                case NOERROR_OK:
                    Log.println("Pairing confirmed");
                    md = FULLY_REGISTERED;
                    break;
                case NOERROR:
                    Log.println("Waiting for an admin tag swipe");
                    md = WAIT_FOR_REGISTER_SWIPE;
                    _pair_cb();
                    break;
            }
            break;
        case WAIT_FOR_REGISTER_SWIPE:
            // display.showString("pair");
            break;
        case FULLY_REGISTERED:
            Debug.println("Pairing done");
            if (_paired_cb)
                _paired_cb();
            md = DONE;
            break;
        case RETRYABLE_FAIL:
        {
            static unsigned long lst = 0;
            static int cnt = 1;
            if (lst == 0) lst = millis();
            if (millis() - lst > cnt * 1000) {
                md =  (cnt++ > 10) ? WIFI_FAIL_REBOOT : WAITING_FOR_NTP;
                lst = 0;
            };
            break;
        }
        case WIFI_FAIL_REBOOT:
            Log.println("Rebooting");
            delay(5000);
            ESP.restart();
            return;
            break;
        default:
            Log.printf("Unexpected state for RestAPI: %d\n", md);
            break;
    };
   
    switch(ret) {
        case NOERROR:
        case NOERROR_OK:
            freezeout = 0;
            return;
            break;
        case ERR_REPAIR:
            freezeout = (freezeout + 250) *2;
            paired = false;
            Log.println("Unpairing and re-starting registration");
            md = WAITING_FOR_NTP;
            break;
        case ERR_RETRYABLE:
            freezeout = (freezeout + 1000) *2;
            Log.println("Re-trying registration");
            md = WAITING_FOR_NTP;
            break;
        case ERR_FATAL:
            if (paired) {
                Log.println("Could not check our paired. But we're paired; so pray we have a table.");
                md = FULLY_REGISTERED;
            } else {
                Log.println("Network is a disaster. Given up on it.");
                md = WIFI_FAIL_REBOOT;
            };
            break;
        default:
            break;
    }
    Log.printf("Freezeout: %lu - ret %d\n", freezeout, ret);
};


extern unsigned char sha256_client[32];

void RestDeck::render_pane(bool refresh) {
    if(!refresh)
        return;


    if (!_restAPI) {
        _display->print_centred("NO REST");
        return;
    };
    _display->print_centred((char *)_restAPI->_terminalName);

    char tmp[128];
    snprintf(tmp,sizeof(tmp),"%s %s", _restAPI->ready() ? "OK" : "pending", _restAPI->getStatLabel());
    _display->print_centred(tmp, false);

    sha256toHEX(sha256_client, tmp);
    const int L = 16;
    char tmp2[L+1];

    _display->setCursor(0,24);
    for(int i = 0; i < 64/L; i++) {
        strncpy(tmp2, tmp + L*i, L); tmp2[L] = '\0';
        _display->print("  ");
        _display->println(tmp2);
    };
};

