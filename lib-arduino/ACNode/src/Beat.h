#ifndef _H_BEAT
#define _H_BEAT

#include <ACBase.h>
#include <ACNode-private.h>

class Beat : public ACSecurityHandler {
public:
    const char * name() { return "Beat"; }

    bool _debug_alive;

    void            begin();
    void            loop();
    
    cmd_result_t    handle_cmd(ACRequest * req);

    acauth_result_t verify(ACRequest * req);
    acauth_result_t secure(ACRequest * req);

private:
    unsigned long last_loop = 0, last_beat = 0;
};

#endif
