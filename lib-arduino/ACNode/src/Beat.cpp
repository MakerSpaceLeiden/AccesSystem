#include <string.h>
#include <stdlib.h>
#include <limits.h>

#include <ACNode-private.h>
#include <Beat.h>

beat_t beat_absdelta(beat_t a, beat_t b) {
	if (a > b)
		return a - b;
	return b - a;
};

Beat::acauth_result_t Beat::verify(ACRequest * req)
{
    //const char * topic, const char * line, const char ** payload);
    char * p = index(req->rest,' ');
    beat_t  b = strtoul(req->rest,NULL,10);
    size_t bl = p - req->rest;

    if (!p || strlen(req->rest) < 10 || b == 0 || b == ULONG_MAX || bl > 12 || bl < 2) {
        Log.printf("Malformed beat <%s> - ignoring.\n", req->rest);
        return DECLINE;
    };
    
    unsigned long delta = beat_absdelta(b, beatCounter);
    
    if ((beatCounter < 3600) || (delta < 120)) {
        beatCounter = b;
        if (delta > 10) {
            Log.printf("Adjusting beat significantly by %lu seconds.\n", delta);
        } else if (delta) {
            Debug.printf("Adjusting beat by %lu seconds.\n", delta);
        }
    } else {
        Log.printf("Good message -- but beats ignored as they are too far off (%lu seconds)\n",delta);
        return FAIL;
    };

    // Strip off, and accept the beat.
    //
    size_t l = bl;
    if (bl >= sizeof(req->beat))
	bl = sizeof(req->beat) -1;

    p = req->rest + bl;
    while(*p == ' ') p++;

    strncpy(req->beat,req->rest, l);
    strncpy(req->rest, p, sizeof(req->rest));
    req->beatExtracted = b;
    
    return OK;
};

Beat::cmd_result_t Beat::handle_cmd(ACRequest * req) {
    if (!strcmp(req->cmd,"beat"))
        return CMD_CLAIMED;

    return CMD_DECLINE;
}

Beat::acauth_result_t Beat::secure(ACRequest * req) {
    char tmp[sizeof(req->payload)];
    beat_t bc = beatCounter;

    snprintf(tmp, sizeof(tmp), BEATFORMAT " %s", bc, req->payload);
    strncpy(req->payload, tmp, sizeof(req->payload));
    
    return PASS;
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
    unsigned long delta = millis() - last_loop;
    if (delta >= 1000UL) {
        unsigned long secs = (delta + 499UL) / 1000UL;
        last_loop += secs * 1000UL;

	if (secs > 3600) {
        	Log.printf("Time warp by <%lu> seconds; delta=%lu, millis()=%lu. sizeof=%u, Coding error ?\n", 
			secs, delta, millis(), sizeof(unsigned long));
	} else {
        	beatCounter += secs;
        };
    }

    if (_debug_alive) {
        if (millis() - last_beat > 3000 && _acnode->isConnected()) {
            send(NULL, "ping");
            last_beat = millis();
        }
    }
    return;
}

