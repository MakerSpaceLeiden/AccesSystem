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
    PAIRING = machinestate.addState("Pairing",  LED::LED_ERROR, 20*1000, PAIRING_FAILED);
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
                 e->name,e->has, e->needs,e->has & e->needs, (e->has & e->needs) == e->needs, e->status() );

    if (e && e->ok()) {
        Log.printf("Received OK to %s on %s for %s\n", machine, operation ? operation : "power" , e->name);

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
	if (_approvedTagsToSentQueued < MAX_QUEUED)
        	_approvedTagsToSent[_approvedTagsToSentQueued++] = ApprovalEntryWithTag(e,tag);

        _approve++;
        return;
    };
    
    if (e) {
        Log.printf("Received a DENIED to power on %s for %s: %s\n", machine, e->name, e->status());
    } else {
       	Log.println("Unknown tag. Denied and scheduled to report.");
        if (_unknownTagsToSentQueued < MAX_QUEUED)
	    safestrncpy(_unknownTagsToSent[_unknownTagsToSentQueued++], tag, RFID_MAX_TAG_STRING_LEN);
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

#if 0
static String epochseconds2iso8601(time_t n) {
        struct tm * t = gmtime(&n);
	char buff[10];
	snprintf(buff,sizeof(buff),"%04d%02d%02d", t->tm_year, 1 + t->tm_mon, t->tm_mday);
	return String(buff);
}
#endif

void ACNodeRest::report(JsonObject report) {
   ApprovalEntry *e = lastApproved();

   JsonObject m = report["usage"].to<JsonObject>();
   m["inUse"] = (e) ? true : false;

   if (e == NULL)
	return;

   m["name"] = e->name;
   m["shortname"] = e->shortname;
   m["uid"] = e->uid;

   m["has"] = e->has;
   m["needs"] = e->needs;
} 


void ACNodeRest::loop() {
    super::loop();

    static unsigned lst = 0;
    if (!(machinestate.isStable() && machinestate.backgroundTaskOk() && millis()-lst > TAG_SEND_INTERVAL && millis() - _lastApprovalTime > 5000))
	return;

    if (_unknownTagsToSentQueued) {
        const char * url = UNKTAG_URL;
	if (_restAPI->rest(url,"tag=" + String(_unknownTagsToSent[--_unknownTagsToSentQueued]))) 
        	Debug.println("Unknown tag reported");
	else 
		Log.println("Reporting unknown tag failed");
    };

    if (_approvedTagsToSentQueued) {
	ApprovalEntryWithTag et = _approvedTagsToSent[--_approvedTagsToSentQueued];

	// HTTP
        _approvalAPI->sendBestEffortTagApproved(et.tag);

	// MQTT old style
	{
	        JsonDocument payload;
	        payload["name"] = et.e.name;
	        payload["machine"] = machine;
	        payload["node"] = moi;
	        payload["userid"] = et.e.uid;
	        payload["acl"] = "approved";
	        payload["cmd"] = "energize";

		// Old style
		String payloadAsString;
		serializeJson(payload,payloadAsString);
		_client.publish("ac/log/master",("JSON="+payloadAsString).c_str());
	};

	// MQTT new style
        // Signed replacement for public message
	{
		char buff[64];
		safesnprintf(buff, sizeof(buff),"%s/%s", moi, machine);

	        JsonDocument payload;
	        payload["iss"] = buff;

	        payload["name"] = et.e.name;

                snprintf(buff, sizeof(buff),"urn:fdc:makerspaceleiden.nl:20130521:user:%s", et.e.uid); // rfc 4198
	        payload["sub"] = buff;

        	payload["iat"] = time(NULL); // needed for replay protection; see RFC 7519 4.1.6

	        payload["scope"] = "energize";
	        payload["res"] = true;

	        String res = jwt_sign(payload);
	        if (res.length()) 
	            _client.publish("ac/jwt", res.c_str());
	};
        
	lst = millis();
    };

    // We have issues with heap fragmentation if we do regular https calls. At some
    // point - we cannot find a 32k block of contineous memory in the 300k or so free
    // at that time. We can only get rid of these AFAIK with a reboot.
    //
    // Check if we need to reboot -- we do this every 3rd day; between 3am and 7am
    // in the morning if we've been idle for at 60 minutes.
    //
    time_t now = time(NULL);
    struct tm * t = localtime(&now);
    if ((t->tm_yday % 3 == 0) &&
        (t->tm_hour >= 3) &&  (t->tm_hour <= 7) &&
        machinestate.safeForOTA() &&
        (machinestate.secondsInThisState() > 3600) && 
        (uptimeInSeconds() > 5*3600)) {
              Log.println("Automatic 3rd day nightly reboot initiated");
              machinestate = MachineState::REBOOT;
    }
}
