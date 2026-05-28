#include "ACNode.h"
#include "MachineState.h"
#include "REST/RestAPI.h"
#include "REST/ApprovalAPI.h"

#ifndef _ACNODEREST_API
#define _ACNODEREST_API
// Note: this thin wedge is quite expensive in terms of memory.
//       around a 160k when empty.
//

class ApprovalEntryWithTag {
public:
    ApprovalEntryWithTag() {};
    ApprovalEntryWithTag(ApprovalEntry *_e, const char *_tag) {
		e = *_e;
		safestrcpy(tag, _tag);
    };

    ApprovalEntry e;
    char tag[RFID_MAX_TAG_STRING_LEN] = "\0";
};

class ACNodeRest : public ACNodeBase {
private:
    typedef ACNodeBase super;
    AsyncWebSocket * _ws = NULL;

public:
    ACNodeRest(const char * machine, const char * ssid, const char * ssid_passwd);
    ACNodeRest(const char * machine = NULL, bool wired = true);

    void CONSTS();
    void pop();
    
    void begin(eth_board_t board = BOARD_AART, uint8_t clear_button = -1);
    void report(JsonObject & report);
    void status(JsonObject & report);
    void loop();

    void request_approval(const char * tag, const char * operation = NULL, const char * target = NULL, bool useCacheOk= true);
    
    MachineState machinestate;
    MachineState::machinestate_t WAIT_FOR_PAIRING, PAIRING_FAILED, PAIRING;

    void clearLastApproved();
    ApprovalEntry * lastApproved();
    
    void sentNotification(String dest, String subject, String msg);


    // Temp unprotected for payment experiments.
    RestAPI * _restAPI;
protected:
    ApprovalAPI *_approvalAPI;
    ApprovalEntry * _lastApproved;
private:
    unsigned long _lastApprovalTime;
   // Que of tags to inform the server about on a best effort basis.

    const unsigned long TAG_SEND_INTERVAL = 5 * 1000; // at least 5 seconds in between tag sends.

    // we've gone from a std::list to something fix to battle fragmentation
    static const unsigned char MAX_QUEUED = 4;

    unsigned char _approvedTagsToSentQueued = 0;
    ApprovalEntryWithTag _approvedTagsToSent[MAX_QUEUED]; 

    unsigned char _unknownTagsToSentQueued = 0;
    char _unknownTagsToSent[MAX_QUEUED][RFID_MAX_TAG_STRING_LEN];
};
#endif
