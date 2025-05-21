#include <Arduino.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MBEDTLS_EXIT_SUCCESS    EXIT_SUCCESS
#define MBEDTLS_EXIT_FAILURE    EXIT_FAILURE

#include "mbedtls/x509_crt.h"
#include "mbedtls/x509_csr.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/md.h"
#include "mbedtls/error.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/base64.h"
#include <mbedtls/sha256.h>

#include <TLog.h>
#include "REST/selfsign.h"
#include "REST/geneckey.h"

#include "esp_log.h"
#include "esp_heap_caps.h"

#define DFL_DIGEST              MBEDTLS_MD_SHA256
#define DFL_NOT_BEFORE          "20211004000000"
#define DFL_NOT_AFTER           "21211004000000"
#define DFL_IS_CA               0
#define DFL_MAX_PATHLEN         -1
#define DFL_KEY_USAGE           MBEDTLS_X509_KU_DIGITAL_SIGNATURE
#define DFL_NS_CERT_TYPE        MBEDTLS_X509_NS_CERT_TYPE_SSL_CLIENT
#define DFL_AUTH_IDENT          1
#define DFL_SUBJ_IDENT          1
#define DFL_CONSTRAINTS         1

#define DEFAULT_PEM_MAX (4*1024) // larger than 1000-ish cert and 100-150 byte key.
static const char seed[] = "selfsign" __DATE__ __TIME__;
static const char TAG[] = "msl-selfsign-acnode";

int pem2der(unsigned char * buff) {
    size_t len = strlen((const char*) buff);
    char * p = (char *) buff, *e = p + len;
    
    if (p[0] == '-') { // chop off the headers.
        p = index((const char*) buff, '\n') + 1;
        if (buff[len - 1 ] == '\n')
            buff[len - 1] = 0;
        e = rindex((const char*) buff, '\n');
        *e = 0;
        if (e[1] == 0) {
            e = rindex((const char*) buff, '\n');
            *e = 0;
        };
    };
    
    if (mbedtls_base64_decode(buff, len, &len, (const unsigned char*) p, e - p)) {
        Log.println("Invalid PEM to DER");
        return -1;
    };
    return len;
}
char * der2pem(const char *what, unsigned char * der, size_t derlen) {
    unsigned char * tmp = NULL, *p, *ep, * pem = NULL;
    size_t len = 0;
    
    mbedtls_base64_encode (NULL, 0, &len, der, derlen);
    pem = (unsigned char *) malloc(len);
    p = tmp = (unsigned char *) malloc(len + len / 64 + 100);
    ep = p + len + 100;
    
    p += snprintf((char *)p, ep - p, "-----BEGIN %s-----\n", what);
    
    if (mbedtls_base64_encode (pem, len, &len, der, derlen) < 0) {
        Log.println("Failed to encode");
        free(pem); free(tmp);
        return NULL;
    }
    
    // insert linebreaks; to turn it into a PEM
    unsigned char *q = pem;
    for (int i = 0 ; i < len; i++) {
        *p++ = *q++;
        if ((i == len - 1) || (i % 64 == 63)) {
            *p++ = '\n';
        };
    };
    p += snprintf((char*)p, ep - p, "-----END %s-----\n", what);
    free(pem);
    return (char *)tmp;
}


#define MBOK(x) { \
if ((ret = (x)) < 0) {\
mbedtls_strerror( ret, buf, sizeof(buf) ); \
ESP_LOGE(TAG, #x " failed. returned -x%02x, %s", (unsigned int) - ret, buf); \
goto exit; \
}; \
}

int populate_self_signed(mbedtls_pk_context * key, const char * CN_or_full_DN, mbedtls_x509write_cert * crt) {
    unsigned char rndbuff[16];
    char buf[48];
    char dn[128];
    const char * cn = NULL;
    int ret;
    
    mbedtls_entropy_context entropy_ctx;
    mbedtls_ctr_drbg_context ctr_drbg;
    
    mbedtls_mpi serial;
    
    mbedtls_entropy_init( &entropy_ctx );
    mbedtls_ctr_drbg_init( &ctr_drbg );
    
    mbedtls_mpi_init( &serial );
    mbedtls_x509write_crt_init( crt );
    
    MBOK(mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func,
                               &entropy_ctx, (const unsigned char *) seed, strlen(seed)));
    
    mbedtls_x509write_crt_set_subject_key( crt, key );
    mbedtls_x509write_crt_set_issuer_key( crt, key );
    
    cn = CN_or_full_DN;
    if (index(cn, ',') == 0) {
        snprintf(dn, sizeof(dn), DFL_SUBJECT_NAME, CN_or_full_DN);
        cn = dn;
    };
    MBOK(mbedtls_x509write_crt_set_subject_name( crt, cn ) );
    
    cn = CN_or_full_DN;
    if (index(cn, ',') == 0) {
        snprintf(dn, sizeof(dn), DFL_ISSUER_NAME, CN_or_full_DN);
        cn = dn;
    };
    
    MBOK(mbedtls_x509write_crt_set_issuer_name( crt, cn ));
    
    mbedtls_x509write_crt_set_version( crt, DFL_VERSION );
    mbedtls_x509write_crt_set_md_alg( crt, DFL_DIGEST );
    
    MBOK(mbedtls_ctr_drbg_random(&ctr_drbg, rndbuff, sizeof(rndbuff)));
    MBOK(mbedtls_mpi_read_binary( &serial, rndbuff, sizeof(rndbuff)));
    MBOK(mbedtls_x509write_crt_set_serial( crt, &serial));
    
    MBOK(mbedtls_x509write_crt_set_validity( crt, DFL_NOT_BEFORE, DFL_NOT_AFTER ));
    
    if (DFL_VERSION == MBEDTLS_X509_CRT_VERSION_3) {
        if (DFL_CONSTRAINTS != 0 )
            MBOK(mbedtls_x509write_crt_set_basic_constraints( crt, DFL_IS_CA, DFL_MAX_PATHLEN ));
        if (DFL_SUBJ_IDENT != 0 )
            MBOK(mbedtls_x509write_crt_set_subject_key_identifier( crt ));
        if (DFL_AUTH_IDENT != 0 )
            MBOK(mbedtls_x509write_crt_set_authority_key_identifier( crt ));
        if (DFL_KEY_USAGE != 0 )
            MBOK(mbedtls_x509write_crt_set_key_usage( crt, DFL_KEY_USAGE ));
        if ( DFL_NS_CERT_TYPE != 0 )
            (mbedtls_x509write_crt_set_ns_cert_type( crt, DFL_NS_CERT_TYPE ));
    };
    
exit:
    return 0;
    
    mbedtls_mpi_free( &serial );
    mbedtls_ctr_drbg_free( &ctr_drbg );
    mbedtls_entropy_free( &entropy_ctx );
    
    if (ret)
        return ret;
}

int sign_and_topem(mbedtls_pk_context * key, mbedtls_x509write_cert * crt,  char ** out_cert_as_pem,  char ** out_key_as_pem)
{
    unsigned char * tmp = NULL, * cp = NULL, *kp = NULL;
    char buf[48];
    int ret;
    
    mbedtls_entropy_context entropy_ctx;
    mbedtls_ctr_drbg_context ctr_drbg;
    
    mbedtls_entropy_init( &entropy_ctx );
    mbedtls_ctr_drbg_init( &ctr_drbg );
    MBOK(mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func,
                               &entropy_ctx, (const unsigned char *) seed, strlen(seed)));
    
    // MBOK(mbedtls_pk_write_key_pem(key, NULL, 0)); /* we cannot get the length yet in this version of mbed@espressif */
    ret = DEFAULT_PEM_MAX;
    kp = tmp  = (unsigned char *) malloc(ret);
    MBOK(mbedtls_pk_write_key_pem(key, tmp, ret));
    
    // MBOK(mbedtls_x509write_crt_pem(crt, NULL, 0, mbedtls_ctr_drbg_random, &ctr_drbg)); /* we cannot get the length yet in this version of mbed@espressif */
    ret = DEFAULT_PEM_MAX;
    cp = tmp = (unsigned char *) malloc(ret);
    MBOK(mbedtls_x509write_crt_pem(crt, tmp, ret, mbedtls_ctr_drbg_random, &ctr_drbg));
    
    ret = 0;
    *out_cert_as_pem = strdup((const char*) cp);
    *out_key_as_pem = strdup((const char*) kp);
exit:
    if (ret)
        free(tmp);
    if (cp)
        free(cp);
    if (kp)
        free(kp);
    
    mbedtls_ctr_drbg_free( &ctr_drbg );
    mbedtls_entropy_free( &entropy_ctx );
    
    return ret;
}



int fingerprint_from_pem(char * buff, unsigned char sha256[256 / 8]) {
    int ret;
    unsigned char * p = (unsigned char*) strdup(buff);
    
    if (((ret = pem2der(p)) < 0 ) ||
        ((ret = mbedtls_sha256_ret(p, ret, sha256, 0)) < 0 ))
    {
        Log.printf("fingerprint_from_pem failed: %02X\n", -ret);
        memset(sha256, 0, 32);
    };
    free(p);
    
    return ret;
};

int fingerprint_from_certpubkey(const mbedtls_x509_crt * crt, unsigned char sha256[256 / 8]) {
    mbedtls_pk_context * pk = (mbedtls_pk_context*) & (crt->pk);
    unsigned char buff[2 * 1024];
    int ret;
    
    if (((ret = mbedtls_pk_write_pubkey_pem(pk , buff, sizeof(buff))) < 0) ||
        ((ret = pem2der(buff)) < 0 ) ||
        ((ret = mbedtls_sha256_ret(buff, ret, sha256, 0)) < 0 ))
    {
        Log.printf("fingerprint_from_certpubkey failed: %02X\n", -ret);
        memset(sha256, 0, 32);
    };
    return ret;
};

#if 0
// there is something odd/broken:
//  mbedtls_x509write_crt_der/pem and mbedtls_pk_write_key_pem/der do not actually yeild bytewise the same output.
// The PEM one is fine; but the DER one appears to lack the outer sequence and they key has some fields unpopulated.


void dump_der_as_pem(const char *what, unsigned char * der, size_t derlen) {
    
    unsigned char * tmp = NULL;
    size_t len = 0;
    Log.printf("-----BEGIN %s-----\n", what);
    
    mbedtls_base64_encode (NULL, 0, &len, der, derlen);
    tmp = (unsigned char *) malloc(len);
    
    if (mbedtls_base64_encode (tmp, len, &len, der, derlen) < 0) {
        Log.println("Failed to encode");
        return;
    }
    
    for (int i = 0 ; i < len; i++) {
        Log.print((char)tmp[i]);
        if ((i == len - 1) || (i % 64 == 63))
            Log.println();
    };
    Log.printf("-----END %s-----\n", what);
    free(tmp);
}
int sign_and_toder(mbedtls_pk_context * key, mbedtls_x509write_cert * crt, unsigned char ** out_cert_as_der, size_t * outcertlenp, unsigned char ** out_key_as_der, size_t * outkeylenp)
{
    unsigned char * der;
    char buf[48];
    int ret;
    int outkeylen = -1, outcertlen = -1;
    
    mbedtls_entropy_context entropy_ctx;
    mbedtls_ctr_drbg_context ctr_drbg;
    
    mbedtls_entropy_init( &entropy_ctx );
    mbedtls_ctr_drbg_init( &ctr_drbg );
    
    // some padding routine deep inside mbed tls insist on this.
    if (!(der = (unsigned char*) heap_caps_malloc(DEFAULT_DER_MAX, MALLOC_CAP_32BIT))) {
        Log.println("Failed to allocate memory");
        goto exit;
    }
    der = (unsigned char *) malloc( 8 * 1024);
    
    MBOK(mbedtls_pk_write_key_der(key, der, 8 * 1024));
    outkeylen = ret;
    
    if ((NULL == *out_key_as_der) && (NULL == (*out_key_as_der = (unsigned char*)malloc(outkeylen)))) {
        ESP_LOGE(TAG, "outkeylen malloc failed (%u bytes)", outkeylen);
        goto exit;
    };
    memcpy(*out_key_as_der, der, outkeylen);
    
    dump_der_as_pem("EC PRIVATE KEY", *out_key_as_der, outkeylen);
    MBOK(mbedtls_pk_write_key_pem(key, der, 8 * 1024));
    Log.println((char*)der);
    
    
    MBOK(mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func,
                               &entropy_ctx, (const unsigned char *) seed, strlen(seed)));
    
    MBOK(mbedtls_x509write_crt_der(crt, der, DEFAULT_DER_MAX, mbedtls_ctr_drbg_random, &ctr_drbg));
    outcertlen = ret;
    
    if ((NULL == *out_cert_as_der) && (NULL == (*out_cert_as_der = (unsigned char*)malloc(outcertlen)))) {
        ESP_LOGE(TAG, "outcertlen malloc failed (%u bytes)", outcertlen);
        free(*out_cert_as_der);
        goto exit;
    }
    memcpy(*out_cert_as_der, der, outcertlen);
    
    Log.println("completion");
    
    MBOK(mbedtls_x509write_crt_pem(crt, der, DEFAULT_DER_MAX, mbedtls_ctr_drbg_random, &ctr_drbg));
    Log.println((char*)der);
    
    dump_der_as_pem("CERTIFICATE", *out_cert_as_der, outcertlen);
    
    ret = 0;
exit:
    free(der);
    
    if (outkeylen <= 0 || outcertlen <= 0)
        return -1;
    
    *outkeylenp = outkeylen;
    *outcertlenp = outcertlen;
    
    mbedtls_ctr_drbg_free( &ctr_drbg );
    mbedtls_entropy_free( &entropy_ctx );
    
    return ret;
}
#endif
