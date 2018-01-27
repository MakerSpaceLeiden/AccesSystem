#include <ACNode.h>

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
ACSecurityHandler::acauth_result_t SIG1::verify(const char * topic, const char * line, const char ** payload) {
    char buff[ MAX_MSG ];
    
    // We only accept things starting with SIG/1*<space>hex<space>
    
    size_t len = strlen(line);
    if (len <72|| strncmp(line, "SIG/1.", 6) != 0)
        return ACSecurityHandler::DECLINE;
    
    if (len > sizeof(buff)-1) {
        Log.println("Failing SIG/1 sigature - far too long");
        return FAIL;
    };
    strncpy(buff,line,sizeof(buff));
    
    char * p = buff;
    
    SEP(sig, "SIG1Verify - no sig", ACSecurityHandler::FAIL);
    SEP(hmacString, "SIG1Verify - no hmac", ACSecurityHandler::FAIL);
    SEP(beatString, "SIG1Verify - no beat", ACSecurityHandler::FAIL);
    
    if (strlen(hmacString) != 64) {
        Log.println("Failing SIG/1 sigature - wrong length hmac");
        return FAIL;
    };
    
    unsigned long beat = strtoul(beatString, NULL, 10);
    if (beat == 0) {
        Log.println("Failing SIG/1 sigature - beat issues");
        return FAIL;
    };
    
    const char * hmac2 = hmacAsHex(passwd, beatString, topic, p);
    if (strcasecmp(hmac2, hmacString)) {
        Log.println("Invalid SIG/1 sigature");
        return FAIL;
    };
    
    size_t i =  beatString - buff;
    *payload = line + i;

    // Leave checking of the beat to the next module in line.
    // so it is a pass, rather than an OK.
    //
    return ACSecurityHandler::PASS;
}

const char * SIG1::secure(const char * topic, const char * line) {
    char msg[1024];
    
    char beatAsString[ MAX_BEAT ];
    snprintf(beatAsString, sizeof(beatAsString), BEATFORMAT, beatCounter);
    
    const char * sig  = hmacAsHex(passwd, beatAsString, topic, p+1);
    snprintf(msg, sizeof(msg), "SIG/1.0 %s %s", sig, line);
    
    return msg;
};

const char * SIG1::cloak(const char * tag) {
    char beatAsString[ MAX_BEAT ];
    snprintf(beatAsString, sizeof(beatAsString), BEATFORMAT, beatCounter);
    
    SHA256 sha256;
    sha256.reset();
    sha256.update((unsigned char*)&beatAsString, strlen(beatAsString));
    // Rather messy - but since we're retiring SIG1 - we won't fix.
    //
    unsigned char uid[32];
    char tagc[MAX_MSG];
    strncpy(tagc, tag, sizeof(tagc));
    int i = 0;
    for(char * tok = strtok(tagc," -"); (i < sizeof(uid)) && tok; tok = strtok(NULL," -")) {
        uid[i++] = atoi(tok);
    }
    sha256.update(uid, i);
    
    unsigned char binresult[sha256.hashSize()];
    sha256.finalizeHMAC(passwd, sizeof(passwd), binresult, sizeof(binresult));
    
    const char * tag_encoded = hmacToHex(binresult);
    
    return tag_encoded;
};


