#ifndef _H_SIG1
#define _H_SIG1

#include "ACBase.h"

class SIG1 : public ACSecurityHandler {
public:
    const char * name = "SIG1";
    
    char * password = NULL;

    acauth_result_t verify(const char * topic, const char * line, const char ** payload);

    const char * secure(const char * topic, const char * line);
    const char * cloak(const char * tag);
} ;

#endif
