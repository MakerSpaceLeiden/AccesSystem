#include <ACNode.h>
#include "SIG1.h"

// Note - none of below HMAC utility functions is re-entrant/t-safe; they all rely
// on some private static buffers one is not to meddle with in the 'meantime'.
//
static const char q2c[] = "0123456789abcdef"; // Do not 'uppercase' -- the HMACs are calculated over it - and hence are case sensitive.

const char * hmacToHex(const unsigned char * hmac) {
    static char hex[2 * HASH_LENGTH + 1];
    char * p = hex;
    
    for (int i = 0; i < HASH_LENGTH; i++) {
        *p++ = q2c[hmac[i] >> 4];
        *p++ = q2c[hmac[i] & 15];
    };
    *p++ = 0;
    
    return hex;
}

static const char * hmacAsHex(const char *sessionkey,
                              const char * beatAsString, const char * topic, const char *payload)
{
    SHA256 sha256;
    
    sha256.reset();
    sha256.update((unsigned char*)&beatAsString, strlen(beatAsString));
    if (topic && *topic) sha256.update(topic, strlen(topic));
    if (payload && *payload) sha256.update(payload, strlen(payload));
    
    unsigned char result[sha256.hashSize()];
    sha256.finalizeHMAC(sessionkey, sizeof(sessionkey), result, sizeof(result));
    
    return hmacToHex(result);
}



// Return
//	PASS	ignored - not my cup of tea.
//	FAIL	failed to authenticate - reject it.
//	OK	authenticated OK - accept.
//
SIG1::acauth_result_t SIG1::verify(ACRequest * req) {
    char buff[ MAX_MSG ];
    
    // We only accept things starting with SIG/1*<space>hex<space>
    
    size_t len = strlen(req->payload);
    if (len <72|| strncmp(req->payload, "SIG/1.", 6) != 0)
        return ACSecurityHandler::DECLINE;
    
    if (len > sizeof(buff)-1) {
        Log.println("Failing SIG/1 sigature - far too long");
        return FAIL;
    };
    strncpy(buff,req->payload,sizeof(buff));
    
    char * p = buff;
    
    SEP(sig, "SIG1Verify failed - no sig", ACSecurityHandler::FAIL);
    SEP(hmacString, "SIG1Verify failed - no hmac", ACSecurityHandler::FAIL);
    SEP(beatString, "SIG1Verify failed - no beat", ACSecurityHandler::FAIL);
    
    
    if (strlen(hmacString) != 64) {
        Log.println("Failing SIG/1 sigature - wrong length hmac");
        return FAIL;
    };
    
    unsigned long beat = strtoul(beatString, NULL, 10);
    if (beat == 0) {
        Log.println("Failing SIG/1 sigature - beat issues");
        return FAIL;
    };
    

    const char * hmac2 = hmacAsHex(passwd, beatString, req->topic, p);
    if (strcasecmp(hmac2, hmacString)) {
        Log.println("Invalid SIG/1 sigature");
        return FAIL;
    };

    strncpy(req->version, sig, sizeof(req->version));
    strncpy(req->beat, beatString, sizeof(req->beat));
    req->beatReceived = beat;
    req->cmd[0] = '\0';
    strncpy(req->rest, buff, sizeof(req->rest));

    // Leave checking of the beat to the next module in line.
    // so it is a pass, rather than an OK.
    //
    return ACSecurityHandler::PASS;
}

SIG1::acauth_result_t SIG1::secure(ACRequest * req) {
    char beatAsString[ MAX_BEAT ];
    char msg[MAX_MSG];
    
    // Bit sucky - but since we're retiring SIG1 - fine for now.
    //
    char * p = index(req->payload,' ');
    if (!p) return ACSecurityHandler::FAIL;
    
    strncpy(req->version, "SIG/1.0", sizeof(req->version));
    strncpy(beatAsString,req->payload, p - req->payload);
    
    const char * sig  = hmacAsHex(passwd, beatAsString, req->topic, p+1);
    snprintf(msg, sizeof(msg), "%s %s %s", req->version, sig, req->payload);
    
    strncpy(req->payload, msg, sizeof(req->payload));
    return ACSecurityHandler::OK;
};

SIG1::acauth_result_t SIG1::cloak(ACRequest * req) {
    char beatAsString[ MAX_BEAT ];
    snprintf(beatAsString, sizeof(beatAsString), BEATFORMAT, req->beatReceived);
    
    SHA256 sha256;
    sha256.reset();
    sha256.update((unsigned char*)&beatAsString, strlen(beatAsString));
    // Rather messy - but since we're retiring SIG1 - we won't fix.
    //
    unsigned char uid[32];
    char tagc[MAX_MSG];
    strncpy(tagc, req->tag, sizeof(tagc));
    
    int i = 0;
    for(char * tok = strtok(tagc," -"); (i < sizeof(uid)) && tok; tok = strtok(NULL," -")) {
        uid[i++] = atoi(tok);
    }
    sha256.update(uid, i);
    
    unsigned char binresult[sha256.hashSize()];
    sha256.finalizeHMAC(passwd, sizeof(passwd), binresult, sizeof(binresult));
    
    const char * tag_encoded = hmacToHex(binresult);
    
    strncpy(req->tag, tag_encoded, sizeof(req->tag));
    
    return ACSecurityHandler::OK;
};


