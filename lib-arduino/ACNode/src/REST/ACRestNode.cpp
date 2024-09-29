#include "Rest/ACRestNode.h"
#include "Rest/rest.h"

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
    _approvalAPI = new ApprovalAPI(_restAPI, machine);

    PAIRING_FAILED = machinestate.addState("Pairing Failed",  LED::LED_ERROR, 5*1000, MachineState::OUTOFORDER);
    PAIRING = machinestate.addState("Pairing",  LED::LED_ERROR, 10*1000, PAIRING_FAILED, MachineState::WAITINGFORCARD);
    WAIT_FOR_PAIRING = machinestate.addState("Needs to pair",  LED::LED_ERROR, 20*1000, MachineState::OUTOFORDER);

    machinestate.setOnChangeCallback(PAIRING_FAILED, [&](MachineState::machinestate_t last, MachineState::machinestate_t current) -> void {
        if (_approvalAPI->canApprove()) {
            Log.println("Could not check pairing - continuing on cache");
            machinestate = MachineState::WAITINGFORCARD;
        };
    });
    
    machinestate.setState(MachineState::BOOTING);
    addHandler(&machinestate);

    _restAPI->setTerminalname(machine);
    _restAPI->onPairingRequested([this](){
        Log.println("Waiting for pairing");
        machinestate = WAIT_FOR_PAIRING;
    });
    
    _restAPI->onPaired([this](){
        Log.println("Pairing OK");
        machinestate = MachineState::WAITINGFORCARD;
    });
    
    addHandler(_restAPI);
    addHandler(_approvalAPI);
}

void ACNodeRest::begin(eth_board_t board, uint8_t clear_button) {
    super::begin(board,clear_button);
    
    if (!isConnected()) {
        Log.println("Offline mode");
        machinestate = MachineState::WAITINGFORCARD;
    };
}

void ACNodeRest::request_approval(const char * tag, const char * operation, const char * target, bool useCacheOk) {        
    _reqs++;
    
    ApprovalEntry * e = _approvalAPI->getEntry(tag);
    if (e && e->ok()) {
        Debug.printf("User: %s, (has=%x & needs=%x) = %x ==> %d\n",
                     e->name.c_str(),e->has, e->needs,e->has & e->needs, (e->has & e->needs) == e->needs );
        Log.printf("Received OK to %s on %s for %s\n", machine, operation ? operation : "power" , e->name.c_str());
#if 0
        JsonDocument payload;
        payload["machine"] = machine;
        payload["member"] = e->name;
        payload["action"] = "power-on";
        payload["permission"] = true;
        String *res = jwt_sign(payload);
        if (res) {
            _client.publish("ac/jwt", res->c_str());
            delete res;
        };
#endif
        if (_lastApproved)
            delete _lastApproved;
        _lastApproved = e;
        
        if (_approved_callback)
            _approved_callback(machine);
        
        _approve++;
        return;
    };
    
    if (e) {
        Log.printf("Received a DENIED to power on %s for %s: %s\n", machine, e->name.c_str(), e->status());
        delete e;
    } else {
        Log.println("Unknown tag. Denied.");
    };
    
    if (_denied_callback)
        _denied_callback("unknown tag");
    
    // Do we want to do a real-check at this point ? With
    // Check if we need to update the database. This may be a user
    // trying soon after a change. Via acl/api/v1/getok/<str:machine>
    // or if we keep it multi machine; via acl/api/v1/getok4node/<str:node>",
    //
    _approvalAPI->scheduleImmediateUpdate();
    _deny++;
};

#if 0
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
#endif
