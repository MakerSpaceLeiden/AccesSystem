#pragma once

#include <ArduinoJson.h>
int rfc4648_base64_decode ( unsigned char * dst,size_t dlen, size_t * olen,const unsigned char * src,size_t slen);

String * generateSignedES256JWT(JsonDocument payload, char * private_key_as_pem);
bool extract_pubkey_from_privkey(const char * cert, const char * public_key, size_t len);
bool extract_pubkey_from_cert(const char * cert, const char * public_key, size_t len);
