#ifndef _H_SIG2
#define _H_SIG2

#include "ACBase.h"


class SIG2 : public ACSecurityHandler {
public:
    const char * name() { return "SIG2"; }
    
    void begin();
    void loop();
    
    cmd_result_t    handle_cmd(ACRequest * req);

    acauth_result_t 	verify(ACRequest * req);
    acauth_result_t    secure(ACRequest * req);
    acauth_result_t    cloak(ACRequest * req);
};
#endif
