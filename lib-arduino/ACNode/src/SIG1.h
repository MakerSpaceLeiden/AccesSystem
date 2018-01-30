#ifndef _H_SIG1
#define _H_SIG1

#include "ACBase.h"

class SIG1 : public ACSecurityHandler {
public:
    const char * name = "SIG1";
    
    char passwd[MAX_NAME] = "";

    acauth_result_t verify(ACRequest * req);

    acauth_result_t secure(ACRequest * req);
    acauth_result_t cloak(ACRequest * req);
} ;

#endif
