#ifndef _H_COMMONS
#define _H_COMMONS

unsigned char binary_to_base64(unsigned char v);
unsigned char base64_to_binary(unsigned char c);
unsigned int encode_base64_length(unsigned int input_length);
unsigned int decode_base64_length(unsigned char input[]);
unsigned int decode_base64_length(unsigned char input[], unsigned int input_length);
unsigned int encode_base64(unsigned char input[], unsigned int input_length, unsigned char output[]);
unsigned int decode_base64(unsigned char input[], unsigned char output[]);
unsigned int decode_base64(unsigned char input[], unsigned int input_length, unsigned char output[]);
unsigned char binary_to_base64(unsigned char v) ;
unsigned char base64_to_binary(unsigned char c) ;
unsigned int encode_base64_length(unsigned int input_length) ;
unsigned int decode_base64_length(unsigned char input[]) ;
unsigned int decode_base64_length(unsigned char input[], unsigned int input_length) ;
unsigned int encode_base64(unsigned char input[], unsigned int input_length, unsigned char output[]) ;
unsigned int decode_base64(unsigned char input[], unsigned char output[]) ;
unsigned int decode_base64(unsigned char input[], unsigned int input_length, unsigned char output[]) ;


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
