#ifndef _H_GENKEY
#define _H_GENKEY
#include <mbedtls/pk.h>
#include <mbedtls/ecdsa.h>

int geneckey(mbedtls_pk_context *key);
#endif
