#ifndef _H_SELFSIGN
#define _H_SELFSIGN

#include <mbedtls/pk.h>
#include <mbedtls/x509_crt.h>

#ifndef RDN_DN_O
#define RDN_DN_O "Hackspaces United"
#endif

#ifndef RDN_DN_L
#define RDN_DN_L "Eastnor Castle"
#endif

#ifndef RDN_DN_C
#define RDN_DN_C "NL"
#endif

#define DFL_VERSION             MBEDTLS_X509_CRT_VERSION_3

#ifndef DFL_SUBJECT_NAME
#define DFL_SUBJECT_NAME        "CN=%s,O=" RDN_DN_O ",L=" RDN_DN_L ",C=" RDN_DN_C
#endif

#ifndef DFL_ISSUER_NAME
#define DFL_ISSUER_NAME         "CN=%s,O=" RDN_DN_O " Self Signers,L=" RDN_DN_L ",C=" RDN_DN_C
#endif

// Convert DER to PEM and vice versa. Buffers assumed static and large enough.
//
char * der2pem(const char *what, unsigned char * der, size_t derlen); // Returns \0 terminated C-str.
int pem2der(unsigned char * buff); // returns length.

char * sha256toHEX(unsigned char sha256[256 / 8], char buff[256 / 4 + 1]);

// Create and (self) sign an x509 certificate. If the DN contains a comma; it is assumed
// to be structured like C=NL,O=Org, ... etc as per mbedtls it standard; and taken as a full
// Distingushed name. Otherwise it is just inserted into a Common Name (CN) field of the DN.
//
int populate_self_signed(mbedtls_pk_context * key, const char * CN_or_full_DN, mbedtls_x509write_cert * crt);
int sign_and_topem(mbedtls_pk_context * key, mbedtls_x509write_cert * crt,  char ** out_cert_as_pem,  char ** out_key_as_pem);

int fingerprint_from_certpubkey(const mbedtls_x509_crt * crt, unsigned char sha256[256/8]);
int fingerprint_from_pem(char * buff, unsigned char sha256[256 / 8]);

#endif
