#ifndef _H_SIG2
#define _H_SIG2

#include "ACBase.h"


class SIG2 : public ACSecurityHandler {
public:
    const char * name = "SIG2";
    
    acauth_result_t verify(ACRequest * req);
    
    void begin();
    void loop();
    
    const char *    secure(ACRequest * req);
    const char *    cloak(ACRequest * req);
    
    cmd_result_t    handle_cmd(ACRequest * req);
} ;
#endif
