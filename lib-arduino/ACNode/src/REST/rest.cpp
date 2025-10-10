#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <Preferences.h>

#include <nvs_flash.h>

#include "mbedtls/md.h"
#include "mbedtls/base64.h"
#include "mbedtls/sha256.h"

#include "mbedtls/x509.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/x509_csr.h"

#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"

#include "mbedtls/error.h"

#include <TLog.h>
#include "REST/rest.h"
#include "REST/geneckey.h"
#include "REST/selfsign.h"
#include "REST/jwt.h"
#include "util/common-utils.h"

#ifndef HTTP_TIMEOUT
#define HTTP_TIMEOUT (5000)
#endif

// mbedtls_x509_crt * client_cert_ptr = NULL, client_cert;

char * ca_root = NULL;
char * nonce = NULL;
char * server_cert_as_pem = NULL;
char * client_cert_as_pem = NULL;
char * client_key_as_pem = NULL;

unsigned char sha256_client[32], sha256_server[32], sha256_server_key[32], sha256_client_pubkey[32];

const char * KS_NAME = "keystore";
const char * KS_KEY_VERSION = "version";
const char * KS_KEY_CLIENT_CRT = "ccap";
const char * KS_KEY_CLIENT_KEY = "ckap";
const char * KS_KEY_SERVER_KEY = "ssk";

const char * stationname = "unset";

const unsigned short KS_VERSION = 0x100;


static const char * h2s(int i) {
    switch(i) {
        case HTTPC_ERROR_CONNECTION_REFUSED  :return "Connection refused";
        case HTTPC_ERROR_SEND_HEADER_FAILED  :return "Could not send request";
        case HTTPC_ERROR_SEND_PAYLOAD_FAILED :return "Could not send body";
        case HTTPC_ERROR_NOT_CONNECTED       :return "Not Connected";
        case HTTPC_ERROR_CONNECTION_LOST     :return "Connection lost";
        case HTTPC_ERROR_NO_STREAM           :return "No Stream";
        case HTTPC_ERROR_NO_HTTP_SERVER      :return "No HTTP";
        case HTTPC_ERROR_TOO_LESS_RAM        :return "Out of memory";
        case HTTPC_ERROR_ENCODING            :return "Encoding fault";
        case HTTPC_ERROR_STREAM_WRITE        :return "Stream write error";
        case HTTPC_ERROR_READ_TIMEOUT        :return "Read timeout";
        default: break;
    };
    return "Unknown HTTP error";
};


#define updateDisplay_progressText(x) { Log.println(x); }
#define updateDisplay()
#define displayForceShowError(x) { Log.println(x); }

bool getks(Preferences keystore, const char * key, char ** dst) {
    size_t len = keystore.getBytesLength(key);
    if (len == 0)
        return false;
    if (*dst == 0)
        *dst = (char *)malloc(len + 1); // assume/know they are strings that need an extra terminating 0
    keystore.getBytes(key, *dst, len);
    (*dst)[len] = 0;
    return true;
}

rest_ret_t setupAuth(const char * terminalName) {
    Preferences keystore;
    bool paired = false;
    
    if (!keystore.begin(KS_NAME, false))
        Log.println("Keystore open failed");
    
    unsigned short version = keystore.getUShort(KS_KEY_VERSION, 0);
    if (version != KS_VERSION ||
        !getks(keystore, KS_KEY_CLIENT_CRT, &client_cert_as_pem) ||
        !getks(keystore, KS_KEY_CLIENT_KEY, &client_key_as_pem) ||
        !keystore.getBytes(KS_KEY_SERVER_KEY, sha256_server_key, 32))
    {
        mbedtls_x509write_cert crt;
        mbedtls_pk_context key;

        Log.println("Incomplete/absent keystore");
        keystore.end();
        
        wipekeys();
        
        if (geneckey(&key)) {
            Log.println("Generation error. Aborting");
            return ERR_RETRYABLE;
        };
        
        if (0 != populate_self_signed(&key, terminalName, &crt)) {
            Log.println("Self sign error. Aborting");
            return ERR_RETRYABLE;
        }
        
        if (0 != sign_and_topem(&key, &crt, &client_cert_as_pem, &client_key_as_pem)) {
            Log.println("Sign/DER error. Aborting");
            return ERR_RETRYABLE;
        };
        Log.printf("Not yet paired. Need working network for this\n", version);
    } else {
        Log.printf("Using existing keys (keystore version 0x%03x), fully configured\n", version);
        paired = true;
        keystore.end();
    };


    mbedtls_x509_crt crt;
    mbedtls_x509_crt_init(&crt);
    if (mbedtls_x509_crt_parse(&crt, (const unsigned char*)client_cert_as_pem, 1+strlen(client_cert_as_pem)) || fingerprint_from_certpubkey(&crt, sha256_client_pubkey)) {
        Log.printf("Certificate appears broken.");
    };

    fingerprint_from_pem(client_cert_as_pem, sha256_client);

    char tmp[65];
    Log.printf("Fingerprint %s for <%s> (as shown in CRM)\n",sha256toHEX(sha256_client, tmp), terminalName ? terminalName : "<unset>" );

    return paired ? NOERROR_OK : NOERROR;
}

String jwt_sign(JsonDocument payload) {
    return generateSignedES256JWT(payload, client_key_as_pem, client_cert_as_pem, sha256_client, sha256_client_pubkey);
}

void wipekeys() {
    Log.println("Wiping keystore");
    nvs_flash_erase(); // erase the NVS partition and...
    nvs_flash_init(); // initialize the NVS partition.

    // Best effort popuplate.
    Preferences keystore;
    keystore.begin(KS_NAME, false);
    keystore.putUShort(KS_KEY_VERSION, KS_VERSION);
    keystore.end();
}

rest_ret_t fetchCA(const char * terminalName) {
    WiFiClientSecure client;
    HTTPClient https;
    rest_ret_t ret = ERR_FATAL;
    
    const mbedtls_x509_crt *peer ;
    bool ok = false;
    
    updateDisplay_progressText("fetching CA");
    
    // Sadly required - due to a limitation in the current SSL stack we must
    // provide the root CA. but we do not know it (yet). So learn it first.
    //
    client.setInsecure();
    if (!https.begin(client, TERMINAL_URL NONE_PATH )) {
        Log.println("Failed to begin https - fetchCA");
        goto exit;
    };
    https.setTimeout(HTTP_TIMEOUT);
    https.setUserAgent(terminalName);
    
    if (https.GET() < 0) {
        Log.println("Failed to begin https (GET, fetchCA)");
        goto exit;
    };
    
    peer = client.getPeerCertificate();
    mbedtls_sha256(peer->raw.p, peer->raw.len, sha256_server, 0);
    server_cert_as_pem = der2pem("CERTIFICATE", peer->raw.p, peer->raw.len);
    
    // Traverse up to (any) root & serialize the CAcert. We need it in
    // PEM format; as that is what setCACert() expects.
    //
    while (peer->next) peer = peer->next;
    ca_root = der2pem("CERTIFICATE", peer->raw.p, peer->raw.len);
    
    updateDisplay_progressText("CA Cert fetched");
    ok = true;
    ret = NOERROR;
    
exit:
    https.end();
    client.stop();
    return ret;
}

rest_ret_t registerDevice(const char * terminalName) {
    WiFiClientSecure client;
    const mbedtls_x509_crt *peer ;
    HTTPClient https;
    int httpCode;
    unsigned char tmp[128], buff[1024], sha256[256 / 8];
    bool ok = false;
    rest_ret_t ret = ERR_FATAL;
    
    client.setCACert(ca_root);
    client.setCertificate(client_cert_as_pem);
    client.setPrivateKey(client_key_as_pem);
    
    terminalName = terminalName ? terminalName : "<unset>";
    
    char *encarg = _argencode((char *) tmp, sizeof(tmp), terminalName);
    
    if (!encarg) {
        Log.println("registerDevice -  tmp buffer too small for agrumens.");
        return ret;
    };
    
    snprintf((char *) buff, sizeof(buff),  TERMINAL_URL REGISTER_PATH "?name=%s", encarg);
    
    if (!https.begin(client, (const char*)buff)) {
        Log.println("Failed to begin https");
        goto exit;
    };
    https.setTimeout(HTTP_TIMEOUT);
    https.setUserAgent(terminalName);
    
    Debug.printf("RegisterDevice Fetch <%s> for terminal <%s>\n", buff, terminalName);
    httpCode =  https.GET();
    
    peer = client.getPeerCertificate();
    if (!peer || peer->raw.len <= 0) {
        Log.println("No peer certificate, Aborting");
        ret = ERR_REPAIR;
        goto exit;
    };
    mbedtls_sha256(peer->raw.p, peer->raw.len, sha256, 0);
    if (memcmp(sha256, sha256_server, 32)) {
        Log.println("Server changed mid registration. Aborting");
        ret = ERR_REPAIR;
        goto exit;
    };

    if (httpCode == HTTP_CODE_OK) {
        Log.printf("We're known/paired");
        // JSON back with name/label, etc.
        ret = NOERROR_OK;
    }
    else if (httpCode == HTTP_CODE_UNAUTHORIZED) {
        if (nonce) {
        	Log.printf("Deleting previous nonce");
		free(nonce);
		nonce = NULL;
	};

        Log.printf("We're not authorized - but got a nonce to try\n");
        nonce = strdup((https.getString().c_str()));
        ret = NOERROR;
    }
    else if (httpCode == HTTP_CODE_NOT_FOUND) {
        Log.printf("Register device failed (not found - is a terminal with the name <%s> configured in the CRM).\n", terminalName);
        ret = ERR_FATAL;
        goto exit;
    } 
    else if (httpCode == HTTP_CODE_FOUND) {
	char tmp[65];
        sha256toHEX(sha256_client, tmp);
	fingerprint_from_pem(client_cert_as_pem, sha256_client);
        Log.printf("Register device failed (there is already terminal with the name <%s> and this fingerprint paired in the CRM).\n", 
		terminalName, sha256toHEX(sha256_client, tmp));
        ret = ERR_FATAL;
        goto exit;
    } 
    else if (httpCode == HTTPC_ERROR_CONNECTION_REFUSED) {
            Log.printf("Register device failed - likely a network problem");
            ret = ERR_RETRYABLE;
            goto exit;
    } else {
        Log.printf("Not gotten the OK(%d)/HTTP_CODE_UNAUTHORIZED(%d) expected; but %d\n", HTTP_CODE_OK, HTTP_CODE_UNAUTHORIZED, httpCode);
        // https.getString().c_str());
        ret = ERR_REPAIR;
        goto exit;
    };
exit:
    https.end();
    client.stop();
    return ret;
};

rest_ret_t registerDeviceSwipe(const char * terminalName, const char * tag) {
    WiFiClientSecure client;
    const mbedtls_x509_crt *peer ;
    HTTPClient https;
    int httpCode;
    unsigned char tmp[128], buff[1024], sha256[256 / 8];
    bool ok = false;
    rest_ret_t ret = ERR_FATAL;
    
    client.setCACert(ca_root);
    client.setCertificate(client_cert_as_pem);
    client.setPrivateKey(client_key_as_pem);
    
    updateDisplay_progressText("sending credentials");
    
    // Create the reply; SHA256(nonce, tag(secret), client, server);
    //
    mbedtls_sha256_context sha_ctx;
    mbedtls_sha256_init(&sha_ctx);
    mbedtls_sha256_starts(&sha_ctx, 0);
    
    // we happen to know that the first two can safely be treated as strings.
    //
    mbedtls_sha256_update(&sha_ctx, (unsigned char*) nonce, strlen(nonce));
    mbedtls_sha256_update(&sha_ctx, (unsigned char*) tag, strlen(tag));
    mbedtls_sha256_update(&sha_ctx, sha256_client, 32);
    mbedtls_sha256_update(&sha_ctx, sha256_server, 32);
    mbedtls_sha256_finish(&sha_ctx, sha256);
    sha256toHEX(sha256, (char*)tmp);
    mbedtls_sha256_free(&sha_ctx);
    
    snprintf((char *) buff, sizeof(buff),  TERMINAL_URL REGISTER_PATH "?response=%s", (char *)tmp);
    
    if (0) {
        Debug.print("nonce=");
        Debug.println(nonce);
        Debug.print("tag=");
        Debug.println(tag);
        Debug.print("client=");
        sha256toHEX(sha256_client, (char*)tmp);
        Debug.println((char *)tmp);
        Debug.print("server=");
        sha256toHEX(sha256_server, (char*)tmp);
        Debug.println((char *)tmp);
        Debug.print("Result=");
        Debug.println((char *)tmp);
    };
    
    if (!https.begin(client, (char *)buff )) {
        Log.println("Failed to begin https");
        goto exit;
    };
    
    httpCode =  https.GET();
    
    peer = client.getPeerCertificate();
    mbedtls_sha256(peer->raw.p, peer->raw.len, tmp, 0);

    if (memcmp(tmp, sha256_server, 32)) {
        Log.println("Server changed mid registration. Aborting");
        ret = ERR_REPAIR;
        goto exit;
    }
    
    // make sure we get a fresh nonce. So it cannot be a nonce timeout.
    // Or should we display, to the user, some error to hint that
    // his/her tag may not be enabled in the CRM - e.g. detect this
    // on the httpCode and distinguish from an old nonce. Requires
    // the 401's for a stale nonce and no-correlation on the backend
    // to change.
    //
    if (httpCode != HTTP_CODE_OK) {
        Log.println("Failed to register");
        ret = (httpCode == HTTPC_ERROR_CONNECTION_REFUSED) ? ERR_RETRYABLE : ERR_REPAIR;
        goto exit;
    }
    
    Log.println("Registration was accepted - we got a nonce");

    mbedtls_sha256_init(&sha_ctx);
    mbedtls_sha256_starts(&sha_ctx, 0);
    mbedtls_sha256_update(&sha_ctx, (unsigned char*) tag, strlen(tag));
    mbedtls_sha256_update(&sha_ctx, sha256, 32);
    mbedtls_sha256_finish(&sha_ctx, sha256);
    sha256toHEX(sha256, (char*)tmp);
    mbedtls_sha256_free(&sha_ctx);
    
    // Compare nonce from server with locally calculated noce as
    // to confirm we are talking to a server that at least can
    // prove it also knows our shared secret (the tag)
    //
    if (!https.getString().equalsIgnoreCase((char*)tmp)) {
        Log.println("Registered OK - but confirmation did not compute. Aborted.");
        ret = ERR_REPAIR;
        goto exit;
    }
    
    // Extract peer cert and calculate hash over the public key; as, especially
    // with Let's Encrypt - the cert itself is regularly renewed.
    //
    if (fingerprint_from_certpubkey(peer, sha256_server_key)) {
        Log.println("Extraction of public key of server failed. Aborted.");
        ret = ERR_FATAL;
        goto exit;
    };
    
    sha256toHEX(sha256_server_key, (char*)tmp);
    Log.print("Server public key SHA256: ");
    Log.println((char*)tmp);
    
    {
        Preferences keystore;
        
        if (!keystore.begin(KS_NAME, false)) {
            Log.println("Keystore open failed");
            ret = ERR_FATAL;
            keystore.end();
            goto exit;
        }
        
        if (keystore.getUShort(KS_KEY_VERSION, 0) != KS_VERSION) {
            Log.println("**** NVS not initialized *****");
            ret = ERR_FATAL;
            keystore.end();
            goto exit;
        }
        
        keystore.putBytes(KS_KEY_CLIENT_CRT, client_cert_as_pem, strlen(client_cert_as_pem));
        keystore.putBytes(KS_KEY_CLIENT_KEY, client_key_as_pem, strlen(client_key_as_pem));
        keystore.putBytes(KS_KEY_SERVER_KEY, sha256_server_key, 32);
        keystore.end();
        
        Log.println("Keys stored in NVS");
    }
    {
        Preferences keystore;
        
        keystore.begin(KS_NAME, false);
        if (keystore.getUShort(KS_KEY_VERSION, 0) != KS_VERSION) {
            Log.println("**** NVS not working 3 *****");
            ret = ERR_FATAL;
            keystore.end();
            goto exit;
        };
        
        Debug.printf("version:            0x%x\n", keystore.getUShort(KS_KEY_VERSION, -1));
        Debug.printf("client_cert_as_pem: 0x%x (len)\n", keystore.getBytesLength(KS_KEY_CLIENT_CRT));
        if (0)
            Debug.printf("client_key_as_pem:  0x%x (len)\n", keystore.getBytesLength(KS_KEY_CLIENT_KEY));
        Debug.printf("sha256_server_key:  0x%x (len)\n", keystore.getBytesLength(KS_KEY_SERVER_KEY));
        
        keystore.end();
    }
    
    Log.println("We are fully paired - we've proven to each other we know the secret & there is no MITM.");
    ok = true;
    
    ret = NOERROR;
exit:
    https.end();
    client.stop();
    return ret;
};

size_t raw_rest(const char * terminalName, const char *url, size_t * maxbufflenp, unsigned char ** buffp, rest_ret_t * ret, String encodedpostargs) {
    WiFiClientSecure client;
    unsigned char sha256[32];
    JsonDocument res;
    HTTPClient https;
    DeserializationError error;
    int len = 0;
    size_t l = 0;
    unsigned char * buff = NULL;
    size_t max;

    *ret = ERR_FATAL;
    
    client.setCACert(ca_root);
    client.setCertificate(client_cert_as_pem);
    client.setPrivateKey(client_key_as_pem);
    
    if (!https.begin(client, url)) {
        Log.println("setup fail");
        return 0;
    };
    https.setTimeout(HTTP_TIMEOUT);
    https.setUserAgent(terminalName);

    Debug.printf("URL(%s): %s\n", encodedpostargs.length() ? "POST" : "GET", url);

    if (encodedpostargs.length())
      https.addHeader("Content-Type", "application/x-www-form-urlencoded");

    int httpCode = encodedpostargs.length() ? https.POST(encodedpostargs) : https.GET();

    if (httpCode < 0) {
        Log.printf("raw_rest - network issue: %s\n", h2s(httpCode));
        goto exit;
    }
    
    if (fingerprint_from_certpubkey( client.getPeerCertificate(), sha256)) {
        Log.println("raw_rest: Extraction of public key of server failed. Aborted.");
        goto exit;
    };
    
    if (0 != memcmp(sha256, sha256_server_key, 31)) {
        Log.println("raw_rest: Server pubkey changed. Aborting");
        *ret = ERR_REPAIR;
        goto exit;
    }
    
    if (httpCode == HTTP_CODE_UNAUTHORIZED) {
        *ret = ERR_REPAIR;
        Log.printf("raw_rest: Unauthorized; repairing\n");
        goto exit;
    };
    
    if (httpCode == HTTP_CODE_FOUND) {
        // Special case; to confirm pairing; with no data sent (should change into some json with config).
        *ret = NOERROR_OK;
        goto exit;
    }
    
    if (httpCode == HTTP_CODE_BAD_REQUEST) {
        Log.printf("raw_rest: bad request; propably lost the pairing\n");
        *ret = ERR_REPAIR;
        goto exit;
    }
    
    
    if (httpCode == HTTP_CODE_NOT_FOUND) {
        Log.printf("raw_rest: not-found: %s(%d): %s\n", https.errorToString(httpCode), httpCode, https.getString().c_str());
        *ret = ERR_RETRYABLE;
        goto exit;
    };
    
    if (httpCode != HTTP_CODE_OK) {
        Log.printf("raw_rest: failed: %s(%d):  %s\n", https.errorToString(httpCode), httpCode, https.getString().c_str());
        *ret = ERR_RETRYABLE;
        goto exit;
    };
    
    len = https.getSize();
    if (len) {
        if (len == -1)
            max = 128 * 1024 * 1024; // Hard cap when the length is unknown (we should propably realloc() for this).
        
        if (len != -1 && len < max)
            max = len;
        
        // Use a temporary buffer if we are not
        // going to do anything with the data.
        if (buffp == NULL)
            max = 1 * 1024;

        // Limit the amount to read if any limit is specified.
        //
        max = ((maxbufflenp && *maxbufflenp && (*maxbufflenp ) < max) ? (*maxbufflenp) : (max));

        if (buffp == NULL || *buffp == NULL)
            buff = (unsigned char *)malloc(max);
        else
            buff = *buffp;
        
        if (buff == NULL) {
            Log.printf("raw_rest: malloc(%lu) failed\n",max);
            *ret = ERR_FATAL;
            goto exit;
        }

        WiFiClient * stream = https.getStreamPtr();
        l = 0;
        unsigned long _lst = millis(), TO = 2500;
        for(unsigned char * p = buff;;) {
	    if (!stream->connected()) {
                if (len != -1)
                   Log.println("Connection closed unexpectedly");
                break; 
            };

      	    int sizeAvailable = stream->available();
            if (sizeAvailable == 0) {
		if (millis() > _lst + TO) {
			Log.println("HTTP read timeout");
			break;
		};
		delay(250);
		continue;
	    };
            size_t left = (buffp == NULL) ? max : max - l; 

            if (left > sizeAvailable)
                  left = sizeAvailable;

            int n = stream->readBytes(p, left);
            if (n <= 0) {
		Log.println("HTTP read error");
		break;
	    };
            if (n != left) {
		Debug.println("HTTP read incomplete, retry");
            };
            _lst = millis();
            TO = 750;

            l+=n;
  	    if (buffp == NULL)
		continue;

	    p += n;
	    if (l >= max)
		break;
        };
    }
    if (buffp == NULL)
        free(buff); // we used a temp buffer - mainly to learn the actual size.
    else if (len != -1 && len != l) {
        Log.printf("Incomplete HTTP read; expected %d, got %d\n", len, l);
        if (*buffp == NULL)
            free(buff);
        l = 0;
    } else if (*buffp == NULL)
        *buffp = buff; // return the allocated buffer if we created one.

    // If we do not know the lenght; set it to what we actually read.
    if (len == -1)
        len = l;

    // Return the amount available when possible.
    //
    if (maxbufflenp)
        *maxbufflenp = len;
    
    *ret = NOERROR;
exit:
    https.end();
    client.stop();
    
    return l; // Return actual length in the buffer
}

JsonDocument raw_rest(const char * terminalName, const char *url, rest_ret_t * retp, String encodedpostargs) {
    JsonDocument res;
    DeserializationError error;
    
    unsigned char * buff = NULL; //
    size_t len = 32 * 1024; // Capped; set to zero to uncap.
    size_t n = raw_rest(terminalName,url,&len,&buff,retp, encodedpostargs);
    
    if (*retp != NOERROR)
        goto exit;
    
    if (len > n) {
        Log.println("raw_rest error - document larger than buffer");
        *retp = ERR_FATAL;
        goto exit;
    };
    
    error = deserializeJson(res, (const char *)buff, n);
    if (error) {
        Log.printf("raw_rest: Deserialize of JSON failed: %s\n", error.c_str());
        *retp = ERR_RETRYABLE;
    };
exit:
    if (buff) free(buff);
    return res;
}

