#ifndef _H_COMMONS
#define _H_COMMONS
#include <mbedtls/base64.h>
#include <string.h>

size_t decode_base64_length(unsigned char * base64str);

#define B64L(n) ((((4 * n / 3) + 3) & ~3)+1)

#define B64DE(base64str, bin, what, errorOnReturn) { \
     if (decode_base64_length((unsigned char *)(base64str)) != sizeof((bin))) { \
            Debug.printf("Wrong length " what " (expected %d, got %d/%s) - ignoring\n", \
                       sizeof((bin)), decode_base64_length((unsigned char *)(base64str)), (base64str)); \
            return (errorOnReturn); \
     }; \
    size_t olen = 0; \
    /* make sure we're not passed pointers - but bonafida blocks  - as mbed_tls wants size. */ \
    assert(sizeof(bin)>8); \
    mbedtls_base64_decode(bin,sizeof(bin),&olen,(unsigned char *)(base64str),strlen(base64str)); \
}

#define B64D(base64str, bin, what) { B64DE(base64str, bin, what, false); }

#define SEP(tok, err, errorOnReturn) \
        char *  tok = strsepspace(&p); \
        if (!tok) { \
                Debug.printf("Malformed/missing " err ": %s\n", p ? p : "<NULL>" ); \
                return errorOnReturn; \
        }; 
#endif
