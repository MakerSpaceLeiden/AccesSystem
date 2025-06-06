#pragma once

#include "BlackNodev111.h"

class BlueNodev114 : public BlackNodev111 {
private:
    typedef BlackNodev111 super;
public:
    BlueNodev114(const char * machine, const char * ssid, const char * ssid_passwd, acnode_proto_t proto = PROTO_SIG2) : BlackNodev111(machine, ssid_passwd, proto) {};
    BlueNodev114(const char * machine = NULL, bool wired = true, acnode_proto_t proto = PROTO_SIG2) : BlackNodev111(machine, wired, proto) {};

    const char * name() { return "BlueNodev114"; }
    
    void CONSTS() {
        BUTT2 = 0; 
    };
};
