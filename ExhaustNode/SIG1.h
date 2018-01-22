#pragma once

#include <Crypto.h>
#include <SHA256.h>

#include <base64.hpp>

#define SIG1

extern const char * hmacToHex(const unsigned char * hmac);
extern const char * hmacAsHex(const char *passwd, const char * beatAsString, const char * topic, const char *payload);
extern void hmac_sign(char * msg, size_t len, const char * beat, const char * payload);
extern bool hmac_valid(const char * hmac_signature, const char *password, const char * beat, const char *topic, const char *payload);


