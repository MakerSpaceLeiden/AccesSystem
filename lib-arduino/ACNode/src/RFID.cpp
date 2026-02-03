#include "ACBaseNode.h"
#include "RFID.h"
#include "Display/Display.h"

volatile bool cardScannedIrqSeen = false;
static void readCard() { cardScannedIrqSeen = true; }

void RFID::registerCallback(unsigned char irqpin) {
    if (irqpin == 255)
        return;
    
    pinMode(irqpin, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(irqpin), readCard, FALLING);
    _irqMode = true;
};

void RFID::processAndRateLimitCard(unsigned char * bintag, size_t len) {
    char tag[RFID_MAX_TAG_STRING_LEN];
    bzero(tag,sizeof(tag));
    for (int i = 0; i < len; i++) {
        char buff[5];
        size_t n = snprintf(buff, sizeof(buff), "%s%d", i ? "-" : "", bintag[i]);
        size_t left = sizeof(tag) - strlen(tag) - 1;
        if (left < n) {
            Log.println("Tag too long - aborting.");
            break;
        }
        strncat(tag, buff, left);
    };
    
    // Limit the rate of reporting. Unless it is a new tag.
    //
    if (strncmp(lasttag, tag, sizeof(lasttag)) || millis() - lastswipe > 3000) {
        lastswipe = millis();
        strncpy(lasttag, tag, sizeof(lasttag));
        
        if (!_swipe_cb || (_swipe_cb(lasttag) != ACNodeBase::CMD_CLAIMED)) {
            // Simple approval request; default is to 'energise' the contactor on 'machine'.
            Debug.printf("Requesting approval\n");
            if (_acnodebase) {
                _acnodebase->request_approval(lasttag);
                return;
            };
            Debug.printf("No callbacks to process tag swipe.");
        };
    } else {;
        Debug.println("Ratelimiting repeated swipe - not passed on.");
    };
}

void RFID::report(JsonObject report) {
    report["rfid_scans"] = _scan;
    report["rfid_misses"] = _miss;
    report["rfid"] = firmwareVersionString();
}

    
void RfidDeck::render_pane(bool refresh) {
    if (!refresh) return;
    _display->print_centred("RFID Reader");
    if (!_reader) {
        _display->printf("None connected");
        return;
    };
    _display->printf("Mfct:%s\n",_reader->name());
    _display->printf("FW  :%s\n",_reader->firmwareVersionString().c_str());
    _display->printf("Diag:%s\n",_reader->stateString().c_str());
}

