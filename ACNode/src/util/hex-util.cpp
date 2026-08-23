#include "util/hex-util.h"

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
