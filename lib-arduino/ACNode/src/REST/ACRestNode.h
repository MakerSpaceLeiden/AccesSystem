#include "ACNode.h"
#include "Machinestate.h"
#include "REST/RestAPI.h"
#include "REST/ApprovalAPI.h"

#ifndef _ACNODEREST_API
#define _ACNODEREST_API
// Note: this thin wedge is quite expensive in terms of memory.
//       around a 160k when empty.
//

class ACNodeRest : public ACNodeBase {
private:
    typedef ACNodeBase super;

public:
    ACNodeRest(const char * machine, const char * ssid, const char * ssid_passwd);
    ACNodeRest(const char * machine = NULL, bool wired = true);

    void CONSTS();
    void pop();
    
    void begin(eth_board_t board = BOARD_AART, uint8_t clear_button = -1);
    // void loop();

    void request_approval(const char * tag, const char * operation = NULL, const char * target = NULL, bool useCacheOk= true);
    
    MachineState machinestate;
    MachineState::machinestate_t WAIT_FOR_PAIRING, PAIRING_FAILED, PAIRING;

    ApprovalEntry * lastApproved() { return _lastApproved; };
protected:
    RestAPI * _restAPI;
    ApprovalAPI *_approvalAPI;
    ApprovalEntry * _lastApproved;
private:
};
#endif
