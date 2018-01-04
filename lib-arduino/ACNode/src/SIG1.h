#pragma once

#define SIG1

extern const char * hmacToHex(const unsigned char * hmac);
extern void hmac_sign(char * msg, size_t len, const char * beat, const char * payload);
extern bool hmac_valid(const char * hmac_signature, const char *password, const char * beat, const char *topic, const char *payload);


