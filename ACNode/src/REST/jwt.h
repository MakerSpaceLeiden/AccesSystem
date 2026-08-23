#pragma once

#include <ArduinoJson.h>
int rfc4648_base64_decode ( unsigned char * dst,size_t dlen, size_t * olen,const unsigned char * src,size_t slen);

bool extract_pubkey_from_privkey(const char * cert, const char * public_key, size_t len);
bool extract_pubkey_from_cert(const char * cert, const char * public_key, size_t len);

String generateSignedES256JWT(JsonDocument & payload, char * private_key_as_pem, char * cert_as_pem, unsigned char sha256_client_cert[32], unsigned char sha256_client_pubkey[32]);
