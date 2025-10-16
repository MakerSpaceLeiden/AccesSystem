#include "REST/ACRestNode.h"
#include "REST/rest.h"

ACNodeRest::ACNodeRest(const char * machine, const char * ssid, const char * ssid_passwd) : super(machine,ssid,ssid_passwd) {
    CONSTS();
    pop();
};

ACNodeRest::ACNodeRest(const char * machine, bool wired) : super(machine, wired) {
    CONSTS();
    pop();
};

void ACNodeRest::CONSTS() {
    // super::CONSTS();
}

void ACNodeRest::pop() {
    // super::pop();
    _restAPI = new RestAPI();
    _approvalAPI = new ApprovalAPI(_restAPI, machine);

    PAIRING_FAILED = machinestate.addState("Pairing Failed",  LED::LED_ERROR, 5*1000, MachineState::OUTOFORDER);
    PAIRING = machinestate.addState("Pairing",  LED::LED_ERROR, 20*1000, PAIRING_FAILED, MachineState::WAITINGFORCARD);
    WAIT_FOR_PAIRING = machinestate.addState("Pairing lost",  LED::LED_ERROR, 30*1000, MachineState::OUTOFORDER);

    machinestate.addOnChangeCallback(PAIRING_FAILED, [&](MachineState::machinestate_t last, MachineState::machinestate_t current) -> void {
        if (_approvalAPI->canApprove()) {
            Log.println("Could not check pairing - continuing on cache");
            machinestate = MachineState::WAITINGFORCARD;
        };
    });
    
    machinestate.addOnChangeCallback(MachineState::WAITINGFORCARD, [&](MachineState::machinestate_t last, MachineState::machinestate_t current) -> void {
        clearLastApproved();
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

    if (e) Debug.printf("User: %s, (has=%x & needs=%x) = %x ==> %d (%s)\n",
                 e->name.c_str(),e->has, e->needs,e->has & e->needs, (e->has & e->needs) == e->needs, e->status() );

    if (e && e->ok()) {
        Log.printf("Received OK to %s on %s for %s\n", machine, operation ? operation : "power" , e->name.c_str());

        if (_lastApproved)
            delete _lastApproved;
        _lastApproved = e;
        _lastApprovalTime = millis();
        
        if (_approved_callback) {
            Debug.println("Calling appproval callback");
            _approved_callback(machine);
        };

	// Do not send it again if it is already in our list to send.
        // if (std::find(std::begin(_approvedTagsToSent), std::end( _approvedTagsToSent), t) != std::end( _approvedTagsToSent))
	{
        	_approvedTagsToSent.push_back(ApprovalEntryWithTag(e,tag));
	}

        _approve++;
        return;
    };
    
    if (e) {
        Log.printf("Received a DENIED to power on %s for %s: %s\n", machine, e->name.c_str(), e->status());
    } else {
        Log.println("Unknown tag. Denied.");
    };
    
    if (_denied_callback)
        _denied_callback(e ? e->status() : "Unknown tag");

    if (e)
        delete e;

    // Do we want to do a real-check at this point ? With
    // Check if we need to update the database. This may be a user
    // trying soon after a change. Via acl/api/v1/getok/<str:machine>
    // or if we keep it multi machine; via acl/api/v1/getok4node/<str:node>",
    //
    _approvalAPI->scheduleImmediateUpdate();
    _deny++;
};

void ACNodeRest::clearLastApproved() {
    if (_lastApproved)
        delete _lastApproved;
    _lastApproved = NULL;
}

ApprovalEntry * ACNodeRest::lastApproved() {
    return _lastApproved;
};

void ACNodeRest::sentNotification(String dest, String subject, String msg) {
    String sender = _lastApproved ? _lastApproved->uid : "";
    _restAPI->sentNotification(sender, dest, subject, msg);
}

static String epochseconds2iso8601(time_t n) {
        struct tm * t = gmtime(&n);
	char buff[10];
	snprintf(buff,sizeof(buff),"%04d%02d%02d", t->tm_year, 1 + t->tm_mon, t->tm_mday);
	return String(buff);
}

void ACNodeRest::loop() {
    super::loop();

    static unsigned lst = 0;
    if (_approvedTagsToSent.size() && machinestate.isStable() && machinestate.backgroundTaskOk() && millis()-lst > TAG_SEND_INTERVAL && millis() - _lastApprovalTime > 500) {
	ApprovalEntryWithTag et = *(_approvedTagsToSent.begin());
        _approvedTagsToSent.pop_front();

        _approvalAPI->sendBestEffortTagApproved(et.tag);
	{
	        JsonDocument payload;
	        payload["name"] = et.e.name;
	        payload["machine"] = machine;
	        payload["node"] = moi;
	        payload["userid"] = et.e.uid.toInt();
	        payload["acl"] = "approved";
	        payload["cmd"] = "energize";

		// Old style
		String payloadAsString;
		serializeJson(payload,payloadAsString);
		_client.publish("ac/log/master",("JSON="+payloadAsString).c_str());
	};

	// Signed replacement for public message
	//
	{
	        JsonDocument payload;
	        payload["iss"] = String(moi) + "/" + String(machine);

	        payload["name"] = et.e.name;
	        payload["sub"] = String("urn:fdc:makerspaceleiden.nl:20130521:user:") + et.e.uid; // rfc 4198

        	payload["iat"] = time(NULL); // needed for replay protection; see RFC 7519 4.1.6

	        payload["scope"] = "energize";
	        payload["res"] = true;

	        String res = jwt_sign(payload);
	        if (res.length()) 
	            _client.publish("ac/jwt", res.c_str());
	};
        
	lst = millis();
    };
}

