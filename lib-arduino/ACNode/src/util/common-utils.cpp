#include "util/common-utils.h"

size_t decode_base64_length(unsigned char * base64str) {
	size_t olen = 0;
        mbedtls_base64_decode(NULL, 0, &olen, base64str,strlen((const char*)base64str));
        return olen;
}

char * strsepspace(char **p) {
    char *q = *p;
    if (p == NULL || *p == NULL)
        return NULL;
    //while(**p == ' ') (*p)++;
    while (**p && **p != ' ') {
        (*p)++;
    };
    if (**p && **p == ' ') {
        // while(**p == ' ') (*p)++;
        **p = 0;
        (*p)++;
        return q;
    }
    if (*q)
        return q;
    return NULL;
}

#ifdef ESP32
#   ifdef __cplusplus
extern "C" {
#   endif
extern uint8_t temprature_sens_read();
#   ifdef __cplusplus
}
#endif

double coreTemp() {
    double   temp_farenheit = temprature_sens_read();
    return ( temp_farenheit - 32. ) / 1.8;
}
#endif // ESP32

