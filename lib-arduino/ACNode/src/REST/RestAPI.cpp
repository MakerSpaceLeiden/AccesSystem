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
            paired = true;
            break;
        case NOERROR:
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

int RestAPI::get(const char *url, size_t * maxbufflenp, unsigned char ** buffp) {
    unsigned char *p = NULL;
    if (buffp) p = *buffp;
    rest_ret_t ret;

    size_t n = raw_rest(_terminalName,url,maxbufflenp,buffp,&ret);

    switch(ret) {
        case NOERROR_OK:
        case NOERROR:
            return n;
            break;
        case ERR_FATAL:
            md = WIFI_FAIL_REBOOT;
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
    
JsonDocument RestAPI::get(const char *url) {
    rest_ret_t ret;

    JsonDocument out = raw_rest(_terminalName, url, &ret);
    switch(ret) {
        case NOERROR_OK:
        case NOERROR:
            return out;
            break;
        case ERR_FATAL:
            md = WIFI_FAIL_REBOOT;
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

void RestAPI::loop()
{
    static unsigned long lst = millis(), freezeout = 0;
    if (freezeout && (millis() - lst < freezeout))
        return;
    lst = millis();
        
    rest_ret_t ret = NOERROR;
    
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
            JsonDocument out = raw_rest(_terminalName, PAY_URL REGISTER_PATH, &ret);
            if (ret == NOERROR_OK) {
                Log.println("Registered & paired up ok");
                md = FULLY_REGISTERED;
            };
        };
            break;
        case REGISTER:
            // display.showString("reg");
            switch(registerDevice(_terminalName)) {
                case NOERROR_OK:
                    Log.println("Pairing confirmed");
                    md = FULLY_REGISTERED;
                    break;
                case NOERROR:
                    Log.println("Waiting for an admin tag swipe");
                    md = WAIT_FOR_REGISTER_SWIPE;
                    break;
            }
            break;
        case WAIT_FOR_REGISTER_SWIPE:
            // display.showString("pair");
            break;
        case FULLY_REGISTERED:
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
            freezeout = (freezeout + 1000) *2;
            paired = false;
            Log.println("Unpairing and re-starting registration");
            md = WAITING_FOR_NTP;
            break;
        case RETRYABLE_FAIL:
            freezeout = (freezeout + 1000) *2;
            Log.println("Re-trying registration");
            md = WAITING_FOR_NTP;
            break;
        case ERR_FATAL:
            Log.println("Rebooting");
            md = WIFI_FAIL_REBOOT;
            break;
        default:
            break;
    }
    Log.printf("Freezeout: %lu - ret %d\n", freezeout, ret);
};

extern unsigned char sha256_client[32];
RestDeck::render_pane(bool refresh) {
    const int L = 16;
    unsigned char tmp[128];unsigned char tmp2[L+1];
    
    _display->println("   -- REST --");
    if (!_restAPI)
        return;
    
    _display->printf("ID: %s", _restAPI->_terminalName);

    sha256toHEX(sha256_client, tmp);
    for(int i = 0; i < 64/L; i++) {
        strncpy(tmp + L*i, tmp2, L); tmp2[L] = '\0';
        _display->print("  ");
        _display->println(tmp2);
    };
    // Also show state ?? and paired yes/no
};
