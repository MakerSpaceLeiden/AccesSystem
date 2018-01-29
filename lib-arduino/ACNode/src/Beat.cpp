#include <Beat.h>

#include <string.h>
#include <stdlib.h>
#include <limits.h>

#include <ACNode.h>


Beat::acauth_result_t Beat::verify(ACRequest * req)
{
    //const char * topic, const char * line, const char ** payload);
    char * p = index(req->rest,' ');
    beat_t  b = strtoul(line,NULL,10);
    size_t bl = req->rest - p;
    
    if (!p || strlen(line) < 10 || b == 0 || b == ULONG_MAX || bl > 12 || bl < 2) {
        Log.printf("Malformed beat <%s> - ignoring.\m", req->rest);
        return ACSecurityHandler::DECLINE;
    };
    
    unsigned long delta = llabs((long long) b - (long long)beatCounter);
    
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

    // Strip off, and accept the beat.
    //
    strncpy(req->beat,req->rest, MIN(sizeof(req->beat),bl));
    strcpy(req->rest, req->rest + bl);
    req->beatExtracted = b;
    
    return ACSecurityHandler::OK;
};

Beat::cmd_result_t Beat::handle_cmd(ACRequest * req) {
    if (!strcmp(cmd,"beat"))
        return Beat::CMD_CLAIMED;

    return Beat::CMD_DECLINE;
}

int Beat::secure(ACRequest * req) {
    char tmp[sizeof(req->payload)];
    
    snprintf(tmp, sizeof(tmp), BEATFORMAT " %s", beatCounter, req->payload);
    strcpy(req->payload, tmp, sizeof(req->payload));
    
    return 0;
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

