#ifndef _H_BEAT
#define _H_BEAT

#include <ACBase.h>
#include <ACNode.h>

class Beat : public ACSecurityHandler {
    const char * name = "Beat";
    bool _debug_alive;
    

    void            begin();
    void            loop();
    
    acauth_result_t verify(ACRequest * req);
    cmd_result_t    handle_cmd(ACRequest * req);

        
    const char *    secure(ACRequest * req);
private:
    unsigned long last_loop = 0, last_beat = 0;
};

#endif
