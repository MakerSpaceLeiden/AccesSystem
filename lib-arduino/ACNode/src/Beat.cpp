#include <Beat.h>

#include <string.h>
#include <stdlib.h>
#include <limits.h>

#include <ACNode.h>


Beat::acauth_result_t Beat::verify(const char * topic, const char * line, const char ** payload) {
    char * p = index(line,' ');
    beat_t  b = strtoul(line,NULL,10);
    
    if (!p || strlen(line) < 10 || b == 0 || b == ULONG_MAX)
        return ACSecurityHandler::DECLINE;

    unsigned long delta = llabs((long long) b - (long long)beatCounter);
    
    // otherwise - only accept things in a 4 minute window either side.
    //
    if ((beatCounter < 3600) || (delta < 120)) {
        beatCounter = b;
        if (delta > 10) {
            Log.printf("Adjusting beat significantly by %lu seconds.\n", delta);
        } else if (delta) {
            Debug.printf("Adjusting beat by %lu seconds.\n", delta);
        }
    } else {
        Log.printf("Good message -- but beats ignored as they are too far off (%lu seconds)\n",delta);
        return ACSecurityHandler::FAIL;
    };
    
    return ACSecurityHandler::OK;
};

Beat::cmd_result_t Beat::handle_cmd(char * cmd, char * rest) {
    if (!strcmp(cmd,"beat"))
        return Beat::CMD_CLAIMED;

    return Beat::CMD_DECLINE;
}


const char * Beat::secure(const char * topic, const char * payload) {
    char tosign[MAX_MSG];
    snprintf(tosign, sizeof(tosign), BEATFORMAT " %s", beatCounter, payload);
    
    return tosign;
};

const char * Beat::cloak(const char * tag) {
    return tag;
};

void Beat::begin() {
    // potentially read it from disk or some other persistent store
    // at some point in the future
    //
    beatCounter = 0;
}

void Beat::loop() {
    // Keepting time is a bit messy; the millis() wrap around and
    // the SPI access to the reader seems to mess with the millis().
    // So we revert to doing 'our own'.
    //
    if (millis() - last_loop >= 1000) {
        unsigned long secs = (millis() - last_loop + 499) / 1000;
        beatCounter += secs;
        last_loop = millis();
    }

    if (_debug_alive) {
        if (millis() - last_beat > 3000 && _acnode->isConnected()) {
            send(NULL, "ping");
            last_beat = millis();
        }
    }

    return;
}

