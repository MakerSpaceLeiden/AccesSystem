#ifndef _H_BEAT
#define _H_BEAT

#include <ACBase.h>

class Beat : public ACSecurityHandler {
    const char * name = "Beat";
    bool _debug_alive;
    

    void            begin();
    void            loop();
    
    acauth_result_t verify(const char * topic, const char * line, const char ** payload);
    cmd_result_t    handle_cmd(char * cmd, char * rest);

        
    const char *    secure(const char * topic, const char * line);
    const char *    cloak(const char * tag);
private:
    unsigned long last_loop = 0, last_beat = 0;
    beat_t ast_beat = 0;

};

#endif
