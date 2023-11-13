#ifndef _H_SIG2
#define _H_SIG2

#include "ACBase.h"
#include "Beat.h"

// Exposed - as it is used during the OTA
// process to prevent key leakage by
// rogue firmware.

extern void wipe_eeprom();

class SIG2 : public Beat {
public:
    const char * name() { return "SIG2"; }
    
    void begin();
    void loop();
    
    cmd_result_t    handle_cmd(ACRequest * req);

    acauth_result_t helo(ACRequest * req);
    acauth_result_t verify(ACRequest * req);
    acauth_result_t secure(ACRequest * req);
    acauth_result_t cloak(ACRequest * req);

    void add_trusted_node(const char *node);
private:
    char _nonce[B64L(HASH_LENGTH)];
    void populate_nonce(const char * seedOrNull, char nonce[B64L(HASH_LENGTH)]);
    void request_trust(int i);
};
#endif

