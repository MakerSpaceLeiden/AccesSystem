#ifndef _H_SIG1
#define _H_SIG1

#include "ACBase.h"

class SIG1 : public ACSecurityHandler {
public:
    const char * name = "SIG1";
    
    char password[MAX_NAME] = "";

    acauth_result_t verify(ACRequest * req);

    int secure(ACRequest * req);
    int cloak(ACRequest * req);
} ;

#endif
