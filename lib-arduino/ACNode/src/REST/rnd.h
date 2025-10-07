#ifndef _H_RND
#define _H_RND
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>

extern mbedtls_entropy_context entropy_ctx;
extern mbedtls_ctr_drbg_context ctr_drbg, *p_ctr_drbg;
void ensure_rnd();
#endif
