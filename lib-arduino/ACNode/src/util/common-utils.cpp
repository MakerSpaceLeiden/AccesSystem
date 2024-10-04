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


static unsigned char hex_digit(unsigned char c) {
    return "0123456789ABCDEF"[c & 0x0F];
};

String encodeargs(std::vector<String> pairs) {
    String out = "";
    if (pairs.size() % 2 != 0)
        return "ARGERROR";
    
    for (int i=0;i<pairs.size();i+=2) {
        char tmp[512];
        if (i) out += "&";
        out += String(_argencode(tmp, sizeof(tmp), pairs[i].c_str()));
        out += "=";
        out += String(_argencode(tmp, sizeof(tmp), pairs[i+1].c_str()));
    };
    return out;
};

char *_argencode(char *dst, size_t n, const char *src)
{
    char c, *d = dst;
    while ((c = *src++) != 0)
    {
        if (c == ' ') {
            *d++ = '+';
        } else if (strchr("!*'();:@&=+$,/?%#[] ", c) || c < 32 || c > 127 ) {
            *d++ = '%';
            *d++ = hex_digit(c >> 4);
            *d++ = hex_digit(c);
        } else {
            *d++ = c;
        };
        if (d + 1 >= dst + n)
            return NULL; // Error - buffer too small.
    };
    *d++ = '\0';
    return dst;
}

char * sha256toHEX(unsigned char sha256[256 / 8], char buff[256 / 4 + 1]) {
    const char _h[] = {
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
    };
    char * p = buff;
    for (int i = 0; i < 32; i++) {
        *p++ = _h[(sha256[i] >> 4) & 0xF];
        *p++ = _h[(sha256[i] >> 0) & 0xF];
    }
    *p++ = 0;
    return buff;
}
