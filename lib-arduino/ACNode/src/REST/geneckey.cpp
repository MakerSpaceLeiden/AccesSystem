#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Arduino.h>


#include <mbedtls/error.h>
#include <mbedtls/pk.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>

#include "REST/geneckey.h"
#include <TLog.h>

// We cannot use CURVE2551 in the older version of Espressif -- as mbedtls does not know its OID.
//
static const mbedtls_ecp_group_id DFL_EC_CURVE = MBEDTLS_ECP_DP_SECP256R1; // MBEDTLS_ECP_DP_CURVE25519
static const char *seed = "geneckey" __DATE__ __TIME__;

// #define DEBUG

int geneckey(mbedtls_pk_context *key)
{
  mbedtls_entropy_context entropy_ctx;
  mbedtls_ctr_drbg_context ctr_drbg;
  char buff[48];
  int ret = 1;

  mbedtls_ctr_drbg_init( &ctr_drbg );
  mbedtls_entropy_init( &entropy_ctx );

  if ( ( ret = mbedtls_ctr_drbg_seed( &ctr_drbg, mbedtls_entropy_func, &entropy_ctx,
                                      (const unsigned char*)seed, strlen(seed))) != 0 ) {
    mbedtls_strerror(ret, buff, sizeof(buff));
    Log.print("mbedtls_ctr_drbg_seed: ");
    Log.println(buff);
    return ret;
  };
  mbedtls_pk_init(key);
  if ( ( ret = mbedtls_pk_setup(key,
                                mbedtls_pk_info_from_type( MBEDTLS_PK_ECKEY ) ) ) != 0 ) {
    mbedtls_strerror(ret, buff, sizeof(buff));
    Log.print("mbedtls_pk_setup: ");
    Log.println(buff);
    goto exit;
  }

  if ((ret = mbedtls_ecp_gen_key(DFL_EC_CURVE, mbedtls_pk_ec(*key),
                                 mbedtls_ctr_drbg_random, &ctr_drbg )) != 0) {
    mbedtls_strerror(ret, buff, sizeof(buff));
    Log.print("mbedtls_ecp_gen_key: ");
    Log.println(buff);
    goto exit;
  }

#ifdef DEBUG
    {
        unsigned char * tmp = (unsigned char *) malloc( 8 * 1024);
        if (tmp == NULL || (ret = mbedtls_pk_write_key_pem(key, tmp, 8 * 1024)) != 0) {
            mbedtls_strerror(ret, buff, sizeof(buff));
            Log.print("mbedtls_pk_write_key_pem: ");
            Log.println(buff);
            goto exit;
        };
        Log.printf("%s\n", tmp);
        free(tmp);
    }
#endif

exit:
  mbedtls_ctr_drbg_free( &ctr_drbg );
  mbedtls_entropy_free( &entropy_ctx );
  return ret;

}
