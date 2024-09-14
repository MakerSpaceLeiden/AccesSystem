#include "Rest/ACRestNode.h"

ACNodeRest::ACNodeRest(const char * machine, const char * ssid, const char * ssid_passwd) : super(machine,ssid,ssid_passwd) {
    CONSTS();
    pop();
};

ACNodeRest::ACNodeRest(const char * machine, bool wired) : super(machine, wired) {
    CONSTS();
    pop();
};

void ACNodeRest::CONSTS() {
    super::CONSTS();
}

void ACNodeRest::pop() {
    super::pop();
    
    _restAPI = new RestAPI();
    _restAPI->setTerminalname(machine);
    
    addHandler(_restAPI);
    
    _approvalAPI = new ApprovalAPI(_restAPI);
    addHandler(_approvalAPI);
}

void ACNodeRest::begin(eth_board_t board, uint8_t clear_button) {
    super::begin(board,clear_button);
}

void ACNodeRest::request_approval(const char * tag, const char * operation, const char * target, bool useCacheOk) {
    _reqs++;
    ApprovalEntry * e = _approvalAPI->getEntry(tag);
    if (!e || !(e->has & e->needs)) {
        if (e)
            Log.printf("Received a DENIED to power on %s for %s\n", machine, e->name.c_str());
        else
            Log.println("Unknown tag. Denied.");
        
        if (_denied_callback)
            _denied_callback(machine);
        
        // Do we want to do a real-check at this point ? With
        // Check if we need to update the database. This may be a user
        // trying soon after a change. Via acl/api/v1/getok/<str:machine>
        // or if we keep it multi machine; via acl/api/v1/getok4node/<str:node>",
        //
        _approvalAPI->scheduleImmediateUpdate();
        _deny++;
    } else {
        Log.printf("Received OK to power on %s for %s (%d & %d)\n", machine, e->name.c_str(), e->has, e->needs);
        if (_approved_callback) {
            _approved_callback(machine);
        };
        _approve++;
    };
    if (e)
        delete e;
};

void ACNodeRest::loop() {
    super::loop();
    
    if (_restAPI->state() != RestAPI::FULLY_REGISTERED)
        return;
    
    static unsigned long lst = 0;
    if (!(lst == 0 || millis() - lst > 3600 * 1000))
        return;
    lst = millis();
    
    // do stuff regularly ?? (approval will handle its own fetches though)
}
