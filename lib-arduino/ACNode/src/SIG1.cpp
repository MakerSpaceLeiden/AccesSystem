#include <ACNode-private.h>
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
#ifdef LEGACY
    SHA256 sha256;
    
    sha256.reset();
    sha256.update((unsigned char*)&beatAsString, strlen(beatAsString));
    if (topic && *topic) sha256.update(topic, strlen(topic));
    if (payload && *payload) sha256.update(payload, strlen(payload));
    
    unsigned char result[sha256.hashSize()];
    sha256.finalizeHMAC(sessionkey, sizeof(sessionkey), result, sizeof(result));
#else
    mbedtls_md_context_t ctx;
    mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;
 
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 0);
    mbedtls_md_hmac_starts(&ctx, (const unsigned char*)sessionkey, sizeof(sessionkey));

    mbedtls_md_hmac_update(&ctx, (const unsigned char*)beatAsString, strlen(beatAsString));
    if (topic && *topic) mbedtls_md_hmac_update(&ctx, (const unsigned char*)topic, strlen(topic));
    if (payload && *payload) mbedtls_md_hmac_update(&ctx, (const unsigned char*)payload, strlen(payload));

    byte result[32];
    mbedtls_md_hmac_finish(&ctx, result);
    mbedtls_md_free(&ctx);
#endif
 
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
    req->beatExtracted = beat;
    req->cmd[0] = '\0';
    strncpy(req->rest, buff, sizeof(req->rest));

    // Leave checking of the beat to the next module in line.
    // so it is a pass, rather than an OK.
    //
    return Beat::verify(req);
}

SIG1::acauth_result_t SIG1::secure(ACRequest * req) {
    char beatAsString[ MAX_BEAT ];
    char msg[MAX_MSG+1];
   
    acauth_result_t r = Beat::secure(req);
    if (r == OK || r == FAIL)
	return r;
 
    // Bit sucky - but since we're retiring SIG1 - fine for now.
    //
    char * p = index(req->payload,' ');
    if (!p) return ACSecurityHandler::FAIL;
    
    strncpy(req->version, "SIG/1.0", sizeof(req->version));
    strncpy(beatAsString,req->payload, p - req->payload);
    
    const char * sig  = hmacAsHex(passwd, beatAsString, req->topic, p+1);
    if (snprintf(msg, sizeof(msg), "%s %s %s", req->version, sig, req->payload) < 0)
	return FAIL;
    
    strncpy(req->payload, msg, sizeof(req->payload));
    return OK;
};

SIG1::acauth_result_t SIG1::cloak(ACRequest * req) {
    char beatAsString[ MAX_BEAT ];
    snprintf(beatAsString, sizeof(beatAsString), BEATFORMAT, req->beatExtracted);
   
#ifdef LEGACY 
    SHA256 sha256;
    sha256.reset();
    sha256.update((unsigned char*)&beatAsString, strlen(beatAsString));
#else
    mbedtls_md_context_t ctx;
    mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;
    
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 0);
    mbedtls_md_hmac_starts(&ctx, (const unsigned char *)passwd, sizeof(passwd));
    
    mbedtls_md_hmac_update(&ctx, (const unsigned char*)beatAsString, strlen(beatAsString));
    mbedtls_md_free(&ctx);
#endif

    // Rather messy - but since we're retiring SIG1 - we won't fix.
    //
    unsigned char uid[32];
    char tagc[MAX_MSG];
    strncpy(tagc, req->tag, sizeof(tagc));
    
    int i = 0;
    for(char * tok = strtok(tagc," -"); (i < sizeof(uid)) && tok; tok = strtok(NULL," -")) {
        uid[i++] = atoi(tok);
    }
#ifdef LEGACY
    sha256.update(uid, i);
    
    unsigned char binresult[sha256.hashSize()];
    sha256.finalizeHMAC(passwd, sizeof(passwd), binresult, sizeof(binresult));
#else
    mbedtls_md_hmac_update(&ctx, uid, i);

    byte binresult[32];
    mbedtls_md_hmac_finish(&ctx, binresult);
#endif
    
    const char * tag_encoded = hmacToHex(binresult);
    
    strncpy(req->tag, tag_encoded, sizeof(req->tag));
    
    return OK;
};

SIG1::acauth_result_t SIG1::helo(ACRequest * req) {
	IPAddress myIp = _acnode->localIP();
    	if (snprintf(req->payload, sizeof(req->payload), 
		"announce %d.%d.%d.%d", myIp[0], myIp[1], myIp[2], myIp[3]) < 0)
			return FAIL;

	return OK;
}


