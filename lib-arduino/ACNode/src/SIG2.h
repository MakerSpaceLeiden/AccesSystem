#ifndef _H_SIG2
#define _H_SIG2

#include "ACBase.h"


class SIG2 : public ACSecurityHandler {
public:
    const char * name = "SIG2";
    
    acauth_result_t verify(const char * topic, const char * line, const char ** payload);
    
    void begin();
    void loop();
    
    const char *    secure(const char * topic, const char * line);
    const char *    cloak(const char * tag);
    
    cmd_result_t    handle_cmd(char * cmd, char * rest);
} ;
#endif
