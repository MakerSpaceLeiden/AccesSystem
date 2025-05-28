#include "ACNode.h"

#include "REST/jwt.h"
#include "REST/selfsign.h"	// for SHA256 hex conversion routine

#include <mbedtls/base64.h>
#include <mbedtls/dhm.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ecdh.h>
#include "mbedtls/pk.h"
#include <mbedtls/md.h>
#include <mbedtls/error.h>
#include <mbedtls/asn1.h>
#include <mbedtls/bignum.h>
#include <mbedtls/asn1.h>
#include <mbedtls/bignum.h>
#include <mbedtls/bignum.h>
#include <mbedtls/x509.h>
#include <mbedtls/x509_crt.h>

#include "util/common-utils.h"
#include "esp_random.h"



#define MBOK(x) { \
    if ((ret = (x)) < 0) {\
        char buf[128];mbedtls_strerror( ret, buf, sizeof(buf) ); \
        ESP_LOGE(TAG, #x " failed at line %d. returned 0x%02x, %s", __LINE__, (unsigned int) - ret, buf); \
        Log.printf(#x " failed at line %d. returned 0x%04x, %s\n", __LINE__, (unsigned int) - ret, buf); \
        goto exit; \
    }; \
}

static void B64toRFC4648(unsigned char * b) {
    for(;*b;b++) {
        if (*b == '+') *b = '-';
        else if (*b == '/') *b = '_';
        else if (*b == '=') *b = '\0';
    };
}

int rfc4648_base64_encode( unsigned char * dst,size_t dlen, size_t * olen,const unsigned char * src,size_t slen)
{
    int ret = mbedtls_base64_encode(dst, dlen, olen, src, slen);
    if (ret) return ret;
    B64toRFC4648(dst);
    *olen = strlen((char *)dst);
    return 0;
}

static const char seed[] = "jwt" __DATE__ __TIME__;
static const char TAG[] = "msl-jwt-acnode";

static size_t ecdsa_asn1_to_raw(unsigned char * sig,  size_t key_len, size_t sig_len)
{
    unsigned char *p = sig;
    const unsigned char *end = sig + sig_len;
    size_t len = 0;
    int ret;
    
    MBOK(mbedtls_asn1_get_tag(&p, end, &len,
                              MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE));
    mbedtls_mpi x;
    mbedtls_mpi_init(&x);
    MBOK(mbedtls_asn1_get_mpi(&p, end, &x)); /* R */
    MBOK(mbedtls_mpi_write_binary(&x, sig, key_len));
    MBOK(mbedtls_asn1_get_mpi(&p, end, &x)); /* S */
    MBOK(mbedtls_mpi_write_binary(&x, sig + key_len, key_len));
    mbedtls_mpi_free(&x);
    return 2 * key_len;
exit:
    return 0;
};

bool extract_pubkey_from_privkey(const char * private_key_as_pem, const char * public_key, size_t len) {
    int ret;
    mbedtls_pk_context ctx;
    mbedtls_pk_init(&ctx);
    MBOK(mbedtls_pk_parse_key(&ctx, (const unsigned char*) private_key_as_pem, strlen(private_key_as_pem) + 1, NULL, 0));
    MBOK(mbedtls_pk_write_pubkey_pem(&ctx, (unsigned char*) public_key, len));
    return true;
exit:
    return false;
}

bool extract_pubkey_from_cert(const char * cert, const char * public_key, size_t len) {
    int ret;
    mbedtls_x509_crt clientcert;
    mbedtls_x509_crt_init(&clientcert);
    MBOK(mbedtls_x509_crt_parse(&clientcert,(unsigned char*) cert, strlen(cert) + 1));
    MBOK(mbedtls_pk_write_pubkey_pem(&(clientcert.pk), (unsigned char*) public_key, len));
    return true;
exit:
    return false;
}

char * shortkey(char * pem) {
    int s = 0;
    for(char *p = pem, *q = pem;*p;p++) {
        if (s == 0 && *p == '\n') s = 1;
        if (s == 1 && *p == '-') { *q = 0; s = 2; break; };
        if ((s == 1) && (*p != '\n')) *q++ = *p;
    };
    return pem;
}


String * generateSignedES256JWT(JsonDocument payload, char * private_key_as_pem,  char * cert_as_pem, unsigned char * sha256 )
{
    mbedtls_entropy_context entropy_ctx;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_pk_context ctx;
    size_t key_len, sig_len, len, n;
    unsigned char * buff, *ptr = buff;
    unsigned char hash[32];
    unsigned char *sig;
    String * out;
    JsonDocument hdr;
    String hdrSerialized, plSerialized;
    size_t nHdrSerialized, nPlSerialized;
    int ret;
    unsigned long t;

    hdr["typ"] = "JWT";
    hdr["alg"] = "ES256";
    
   char pubkey[ 2 * strlen(private_key_as_pem)];
    if (extract_pubkey_from_privkey(private_key_as_pem, pubkey, sizeof(pubkey))) {
       	hdr["kid"] = shortkey(pubkey); // Or do we want the SHA256 of the pubkey or Cert here ??
       	hdr["jwk"] = shortkey(pubkey);
    };

    if(sha256) {
    	unsigned char tmp[128];
	MBOK(rfc4648_base64_encode(tmp, sizeof(tmp), &n, (const unsigned char*)sha256, 32));
	// See section 4.1.8 in RFC 7515
	hdr["x5t#S256"] = String((char*)tmp,n);
    };

    // https://www.rfc-editor.org/rfc/rfc7515#section-4.1.6:wq
    if (cert_as_pem) {
	unsigned char * buff = (unsigned char *)strdup(cert_as_pem);
	int l = pem2der(buff); // will fit; DER always shorter.
	unsigned char tmp[ l * 2 ];
	size_t n;

    	rfc4648_base64_encode(tmp, sizeof(tmp), &n, (const unsigned char*)buff, l);
	free(buff);
	
	// Order; from signing cert up to root.
	JsonArray certs = hdr["x5c"].to<JsonArray>();
	certs.add(String(tmp,n));
    };
    nHdrSerialized = serializeJson(hdr, hdrSerialized);
    nPlSerialized = serializeJson(payload, plSerialized);
    
    // size including extra base64 stuff, the '.'s and the final signature.
    //
    len = B64L(hdrSerialized.length()) + 1 + B64L(plSerialized.length()) + 1 + B64L(128) + 1;
    ptr = buff = (unsigned char*) malloc(len);
    
    MBOK(rfc4648_base64_encode(ptr, len + buff - ptr, &n, (const unsigned char*)hdrSerialized.c_str(), hdrSerialized.length()));
    ptr += n;

    *ptr++ = '.'; // Separator header/payload

    MBOK(rfc4648_base64_encode(ptr, len + buff - ptr, &n, (const unsigned char*)plSerialized.c_str(), plSerialized.length()));
    ptr += n;

    // Hash the data we're sigining/protecting; i.e .the serialized header and the payload
    // in RFC 4648 hashed format.
    //
    MBOK(mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), buff, ptr - buff, hash));
    
    mbedtls_entropy_init( &entropy_ctx);
    mbedtls_ctr_drbg_init( &ctr_drbg);
    MBOK(mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func,
                               &entropy_ctx, (const unsigned char *) seed, strlen(seed)));
    
    mbedtls_pk_init(&ctx);
    MBOK(mbedtls_pk_parse_key(&ctx, (const unsigned char*) private_key_as_pem, strlen(private_key_as_pem) + 1, NULL, 0));

    key_len = mbedtls_pk_get_len(&ctx);
    sig_len = key_len * 2 + 10;
    sig  = (unsigned char *) calloc(1, sig_len);
   
    // Sign the hash 
    MBOK(mbedtls_pk_sign(&ctx, MBEDTLS_MD_SHA256, hash, sizeof(hash),
                         sig, &sig_len, mbedtls_ctr_drbg_random, &ctr_drbg));
    sig_len = ecdsa_asn1_to_raw(sig, key_len, sig_len);
    
    *ptr++ = '.'; // Separator payload/signature

    MBOK(rfc4648_base64_encode(ptr, len + buff - ptr, &n, sig, sig_len));
    ptr += n;
    
    out = new String((char*)buff);
    free(buff);

    mbedtls_pk_free(&ctx);
    mbedtls_ctr_drbg_free( &ctr_drbg );
    mbedtls_entropy_free( &entropy_ctx );
exit:
    return out;
}
