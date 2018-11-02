#ifndef _H_COMMONS
#define _H_COMMONS

#define B64L(n) ((((4 * n / 3) + 3) & ~3)+1)

#define B64DE(base64str, bin, what, errorOnReturn) { \
if (decode_base64_length((unsigned char *)base64str) != sizeof(bin)) { \
Debug.printf("Wrong length " what " (expected %d, got %d/%s) - ignoring\n", \
        sizeof(bin), decode_base64_length((unsigned char *)base64str), base64str); \
return errorOnReturn; \
}; \
decode_base64((unsigned char *)base64str, bin); \
}

#define B64D(base64str, bin, what) { B64DE(base64str, bin, what, false); }

#define SEP(tok, err, errorOnReturn) \
        char *  tok = strsepspace(&p); \
        if (!tok) { \
                Debug.printf("Malformed/missing " err ": %s\n", p ? p : "<NULL>" ); \
                return errorOnReturn; \
        }; 
#endif
