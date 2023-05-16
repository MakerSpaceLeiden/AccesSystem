#include "common-utils.h"

size_t decode_base64_length(unsigned char * base64str) {
	size_t olen = 0;
        mbedtls_base64_decode(NULL, 0, &olen, base64str,strlen((const char*)base64str));
        return olen;
}
