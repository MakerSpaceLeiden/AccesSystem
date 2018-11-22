#include <ACNode.h>
#include "ConfigPortal.h"
#include <Cache.h>

// Sort of a fake singleton to overcome callback
// limits in MQTT callback and elsewhere.
//
ACNode * _acnode;
ACLog Log;
ACLog Debug;

#ifdef HAS_MSL
MSL msl = MSL();    // protocol doors (private LAN)
#endif

#ifdef HAS_SIG1
SIG1 sig1 = SIG1(); // protocol machines 20015 (HMAC)
#endif

#ifdef HAS_SIG2
SIG2 sig2 = SIG2();
#endif

#ifdef __cplusplus
extern "C" {
#endif
uint8_t temprature_sens_read();
#ifdef __cplusplus
}
#endif
static double coreTemp() {
  double   temp_farenheit = temprature_sens_read();
  return ( temp_farenheit - 32. ) / 1.8;
}

beat_t beatCounter = 0;      // My own timestamp - manually kept due to SPI timing issues.

void ACNode::set_mqtt_host(const char *p) { strncpy(mqtt_server,p, sizeof(mqtt_server)); };
void ACNode::set_mqtt_port(uint16_t p)  { mqtt_port = p; };
void ACNode::set_mqtt_prefix(const char *p)  { strncpy(mqtt_topic_prefix,p, sizeof(mqtt_topic_prefix)); };
void ACNode::set_mqtt_log(const char *p)  { strncpy(logpath,p, sizeof(logpath)); };
void ACNode::set_moi(const char *p)  { strncpy(moi,p, sizeof(moi)); };
void ACNode::set_machine(const char *p)  { strncpy(machine,p, sizeof(machine)); };
void ACNode::set_master(const char *p)  { strncpy(master,p, sizeof(master)); };

void ACNode::pop() {
    strncpy(mqtt_server, MQTT_SERVER, sizeof(mqtt_server));
    mqtt_port = MQTT_DEFAULT_PORT;
    _report_period = REPORT_PERIOD;

    strncpy(moi, String("node-" + macAddressString() ).c_str(), sizeof(moi));

    strncpy(mqtt_topic_prefix, MQTT_TOPIC_PREFIX, sizeof(mqtt_topic_prefix));
    strncpy(master, MQTT_TOPIC_MASTER, sizeof(master));
    strncpy(logpath, MQTT_TOPIC_LOG, sizeof(logpath));
};

ACNode::ACNode(const char * m, bool wired, acnode_proto_t proto) : 
	_ssid(NULL), _ssid_passwd(NULL), _wired(wired), _proto(proto)
{
    _acnode = this;
    if (m && *m)
      strncpy(machine,m, sizeof(machine));
    pop();
}

ACNode::ACNode(const char *m, const char * ssid , const char * ssid_passwd, acnode_proto_t proto ) :
   	_ssid(ssid), _ssid_passwd(ssid_passwd), _wired(false), _proto(proto)
{
    _acnode = this;
    if (m && *m)
      strncpy(machine,m, sizeof(machine));
    pop();
}

void send(const char * topic, const char * payload) {
    _acnode->send(topic,payload);
}

void ACNode::set_debugAlive(bool debug) { _debug_alive = debug; }

bool ACNode::isConnected() {
    if (_wired)
        return eth_connected();
    return (WiFi.status() == WL_CONNECTED);
};

void ACNode::addHandler(ACBase * handler) {
    _handlers.insert (_handlers.end(), handler);
}

void ACNode::addSecurityHandler(ACSecurityHandler * handler) {
    _security_handlers.insert(_security_handlers.end(), handler);
    
    // Some handlers need a begin or loop maintenance cycle - so we
    // also add these to the normal loop.
    addHandler(handler);
}

void ACNode::begin() {
#if 0
    if (_debug)
        debugFlash();
#endif
    
    if (_wired) {
        eth_setup();
    };
    if (!*machine)  
	strncpy(machine, "unset-machine-name", sizeof(moi));
   
    if (!*moi) 
	strncpy(moi,machine, sizeof(moi));
 
#ifdef CONFIGAP
    configBegin();
    
    // Go into Config AP mode if the orange button is pressed
    // just post powerup -- or if we have an issue loading the
    // config.
    //
    static int debounce = 0;
    while (digitalRead(PUSHBUTTON) == 0 && debounce < 5) {
        debounce++;
        delay(5);
    };
    if (debounce >= 5 || configLoad() == 0)  {
        configPortal();
    }
#endif
    
    if (_wired) {
        WiFi.mode(WIFI_STA);
    } else
    if (_ssid) {
        Serial.printf("Starting up wifi (hardcoded SSID <%s>)\n", _ssid);
        WiFi.begin(_ssid, _ssid_passwd);
    } else {
        Serial.println("Staring wifi auto connect.");
        WiFiManager wifiManager;
        wifiManager.autoConnect();
    };
    
    const int del = 10; // seconds.
    
    // Try up to del seconds to get a WiFi connection; and if that fails; reboot
    // with a bit of a delay.
    //
    unsigned long start = millis();
    while (!isConnected() && (millis() - start < del * 1000)) {
        delay(100);
    };
    
    if (!_wired && !isConnected()) {
        Log.printf("No connection after %d seconds (ssid=%s). Going into config portal (debug mode);.\n", del, WiFi.SSID().c_str());
        // configPortal();
        Log.printf("No connection after %d seconds (ssid=%s). Rebooting.\n", del, WiFi.SSID().c_str());
        Log.println("Rebooting...");
        delay(1000);
        ESP.restart();
    }
    if(_ssid)
        Log.printf("Wifi connected to <%s>\n", WiFi.SSID().c_str());
   
    Log.print(moi); Log.print(" "); Log.println(localIP());
 
    Log.begin();
    Debug.begin(); 

    _espClient = WiFiClient();
    _client = PubSubClient(_espClient);
    
    configBegin();
    configureMQTT();
  
 
    if (_debug)
        debugListFS("/");

    switch(_proto) {
    case PROTO_MSL:
#ifdef HAS_MSL
       addSecurityHandler(&msl);
#endif
	break;
    case PROTO_SIG1:
#ifdef HAS_SIG1
       	addSecurityHandler(&sig1);
	break;
#endif
   case PROTO_SIG2:
#ifdef HAS_SIG2
       	addSecurityHandler(&sig2);
	break;
#endif
   case PROTO_NONE:
        // Lets hope they are added `higher up'.
        break;
  };
  if (_security_handlers.size() == 0) 
	Log.println("*** WARNING -- no protocols defined AT ALL. This is prolly not what you want.");

    // Note that this will also run the security and ohter handlers; see
    // addSecurityHandler().
    //
    {
        std::list<ACBase *>::iterator it;
        for (it =_handlers.begin(); it!=_handlers.end(); ++it) {
   	    Debug.printf("%s.begin()\n", (*it)->name());
            (*it)->begin();
        }
    }
  // secrit reset button that resets TOFU or the shared
  // secret.
  if (digitalRead(SW1_BUTTON) == LOW) {
    extern void wipe_eeprom();
    Log.println("Wiped EEPROM with crypto stuff (SW1 pressed)");
    wipe_eeprom();
    prepareCache(true);  
  } else {
    prepareCache(false);  
  };
}

char * ACNode::cloak(char * tag) {
    ACRequest q = ACRequest();
    strncpy(q.tag, tag, sizeof(q.tag));
    std::list<ACSecurityHandler *>::iterator it;
    for (it =_security_handlers.begin(); it!=_security_handlers.end(); ++it) {
        int r = (*it)->cloak(&q);
        switch(r) {
            case ACSecurityHandler::DECLINE:
                break;
            case ACSecurityHandler::PASS:
                break;
            case ACSecurityHandler::OK:
		Debug.printf("%s -> %s cloaked by %s\n", tag, q.tag, (*it)->name());
    		strncpy(tag, q.tag, MAX_MSG);
		return tag;
                break;
            case ACSecurityHandler::FAIL:
            default:
                Log.printf("Erorr during cloaking (%s) - failing.\n", (*it)->name());
                return NULL;
                break;
        };
    }
    return NULL;
}

void ACNode::request_approval(const char * tag, const char * operation, const char * target) { 
	if (tag == NULL) {
		Log.println("invalid tag==NULL passed, approval request not sent");
		return;
	};
 	if (operation == NULL) 
		operation = "energize";

	if (target == NULL) 
		target = machine;

        strncpy(_lasttag, tag, sizeof(_lasttag));
	if (_approved_callback && checkCache(_lasttag)) {
                _approved_callback(machine);
	};

	char tmp[MAX_MSG];
	strncpy(tmp, tag, sizeof(tmp));
	if (!(cloak(tmp))) {
		Log.println("Coud not cloak the tag, approval request not sent");
		return;
	};

	Debug.printf("Requesting approval for %s at node %s on machine %s by tag %s\n", 
		operation ? operation : "<null>", moi ? moi: "<null>", operation ? operation : "<null>", tag ? tag : "<null>");

        char buff[MAX_MSG];
	snprintf(buff,sizeof(buff),"%s %s %s %s", operation, moi, target, tmp);

        _lastSwipe = beatCounter;
        _reqs++;
	send(NULL,buff);
};

void ACNode::loop() {
    {
	static unsigned long last = 0, lastCntr = 0, Cntr = 0;
	Cntr++;
	if (millis() - last > 30 * 1000) {
		float rate =  1000. * (Cntr - lastCntr)/(millis() - last) + 0.05;
		if (rate > 10)
			Debug.printf("Loop rate: %.1f #/second\n", rate);
		else
			Log.printf("Warning: LOW Loop rate: %.1f #/second\n", rate);
		last = millis();
		lastCntr = Cntr;
	}
    }
    {	static unsigned long last = 0;
	if (millis() - last > _report_period) {
		last = millis();

		DynamicJsonBuffer  jsonBuffer(JSON_OBJECT_SIZE(30) + 500);
		JsonObject& out = jsonBuffer.createObject();
		out[ "node" ] = moi;
		out[ "machine" ] = machine;

                out[ "ip" ] = String(localIP()).c_str();
                out[ "net" ] = _wired ? "UTP" : "WiFi";
  		out[ "mac" ] = macAddressString();

		out[ "beat" ] = beatCounter;

		if (beatCounter > 1542275849 && _start_beat == 0)
			_start_beat  = beatCounter;
		else 
		if (_start_beat)
			out[ "alive-uptime" ] = beatCounter - _start_beat;

		out[ "approve" ] = _approve;
		out[ "deny" ] = _deny;
		out[ "requests" ] = _reqs;

		out[ "cache_hit" ] =  cacheHit;
		out[ "cache_miss" ] =  cacheMiss;

		out[ "mqtt_reconnects" ] = _mqtt_reconnects;

           	out["coreTemp"]  = coreTemp(); 
		out["heap_free"] = ESP.getFreeHeap();	

		std::list<ACBase *>::iterator it;
       		for (it =_handlers.begin(); it!=_handlers.end(); ++it) 
        		(*it)->report(out);

		if (_report_callback) 
			_report_callback(out);	

		char buff[MAX_MSG];
		out.printTo(buff,sizeof(buff));
		Log.println(buff);
        }
    }
    // XX to hook into a callback of the ethernet/wifi
    // once we figure out how we can get this from the wifi.
    //
    static bool lastconnectedstate = false;
    bool connectedstate = isConnected();
    if (lastconnectedstate != connectedstate) {
        if (connectedstate) {
            _connect_callback();
	} else {
            _disconnect_callback();
	};
        lastconnectedstate = connectedstate;
    };
  
    Log.loop();
    Debug.loop();
 
    if(isConnected())
        mqttLoop();
    
    // Note that this will also run the security and ohter handlers; see
    // addSecurityHandler().
    //
    std::list<ACBase *>::iterator it;
 
       for (it =_handlers.begin(); it!=_handlers.end(); ++it) {
        (*it)->loop();
    }

#if 0
    if (_debug) {
        // Emit the state of the node very 5 seconds or so.
        static int last_debug = 0, last_debug_state = -1;
        if (millis() - last_debug > 5000 || last_debug_state != machinestate) {
            Log.print("State: ");
            Log.print(machinestateName[machinestate]);
            Log.print(" Wifi= ");
            Log.print(WiFi.status());
            Log.print(WiFi.status() == WL_CONNECTED ? "(connected)" : "");
            Log.print(" MQTT=<");
            Log.print(state2str(client.state()));
            Log.print(">");
            
            Log.print(" Button="); Log.print(digitalRead(PUSHBUTTON)  ? "not-pressed" : "PRESSed");
            Log.print(" Relay="); Log.print(digitalRead(RELAY)  ? "ON" : "off");
            Log.println(".");
            
            last_debug = millis();
            last_debug_state = machinestate;
        }
    };
#endif
}

ACBase::cmd_result_t ACNode::handle_cmd(ACRequest * req)
{
    if (!strncmp("ping", req->cmd, 4)) {
        char buff[MAX_MSG];
        IPAddress myIp = localIP();
        
        snprintf(buff, sizeof(buff), "ack %s %s %d.%d.%d.%d", master, moi, myIp[0], myIp[1], myIp[2], myIp[3]);
        send(NULL, buff);
	Debug.println("replied on the pick with an ack.");
        return ACNode::CMD_CLAIMED;
    }
    bool app = !strcasecmp("approved",req->cmd);
    bool den = !strcasecmp("denied", req->cmd);

    if (app) _approve++;
    if (den) _deny++;

    if (app || den) {
      char tmp[MAX_MSG], *p = tmp;
      strncpy(tmp, req->rest, sizeof(tmp));

      SEP(action, "No action in approval command", ACNode::CMD_CLAIMED)
      SEP(machine, "No machine-name in approval command", ACNode::CMD_CLAIMED);
      SEP(bcstr, "No nonce/beat in approval command", ACNode::CMD_CLAIMED);
      beat_t bc = strtoul(bcstr, NULL, 10);

      if (beat_absdelta(beatCounter, _lastSwipe) > 60)  {
          Log.printf("Stale energize/denied command received - ignored.\n");
          return ACNode::CMD_CLAIMED;
      };

      if (bc != _lastSwipe) {
          Log.printf("Out of order energize/denied command received - ignored.\n");
          return ACNode::CMD_CLAIMED;
      };

      setCache(_lasttag, app, (unsigned long) beatCounter);

      if (app) {
         Log.printf("Received OK to power on %s\n", machine);
         if (_approved_callback) {
		_approved_callback(machine);
        	return ACNode::CMD_CLAIMED;
         };
     } else {
         Log.printf("Received a DENID to power on %s\n", machine);
         if (_denied_callback) {
		_denied_callback(machine);
        	return ACNode::CMD_CLAIMED;
         };
     }
    }
#if 0
    if (!strcmp("outoforder", req->cmd)) {
        machinestate = OUTOFORDER;
        send(NULL, "event outoforder");
        return ACNode::CMD_CLAIMED;
    }
#endif
    return ACNode::CMD_DECLINE;
}

void ACNode::process(const char * topic, const char * payload)
{
    size_t length = strlen(payload);
    char * p;
   
    Debug.print("["); Debug.print(topic); Debug.print("] <<: ");
    Debug.print((char *)payload);
    Debug.println();
    
    if (length < 6 + 2 * HASH_LENGTH + 1 + 12 + 1) {
        Log.println("Too short - ignoring.");
        return;
    };
    
    ACRequest * req = new ACRequest(topic, payload);
    
    ACSecurityHandler::acauth_results r = ACSecurityHandler::FAIL;
    for (std::list<ACSecurityHandler *>::iterator it =_security_handlers.begin();
         it!=_security_handlers.end() && r != ACSecurityHandler::OK;
         ++it)
    {
        r = (*it)->verify(req);
        switch(r) {
            case ACSecurityHandler::DECLINE:
                Debug.printf("%s could not parse this payload, trying next.\n", (*it)->name());
                break;
            case ACSecurityHandler::PASS:
                Trace.printf("OK payload with %s signature - passing on to next.\n", (*it)->name());
                break;
            case ACSecurityHandler::OK:
                Trace.printf("OK payload with %s signature - handling.\n", (*it)->name());
                break;
            case ACSecurityHandler::FAIL:
            default:
		// rely on the handler to have already done a more meaningful Log. message.
                Debug.printf("Invalid/unknown payload or signature (%s) - failing.\n", (*it)->name());
                goto _done;
                break;
        };
    	Trace.printf("Post %s verify\n\tV=%s\n\tB=%s\n\tC=<%s>\n\tP=<%s>\n\tR=<%s>\n\t=<%s>\n",  
		(*it)->name(), req->version, req->beat, req->cmd, req->payload, req->rest, payload);
    }
    if (r != ACSecurityHandler::OK) {
        Log.println("Unrecognized payload. Ignoring.");
        goto _done;
    }

    // We have a validatd command; so make rest purely the arguments.
    // not sure if we should do this here - or within each handler.
    p = index(req->rest,' ');
    if (p) {
	while(*p == ' ') p++;
	strncpy(req->rest, p, sizeof(req->rest));
    };

    Trace.printf("Post verify\n\tV=%s\n\tB=%s\n\tC=<%s>\n\tP=<%s>\n\tR=<%s>\n\t=<%s>\n", 
	req->version, req->beat, req->cmd, req->payload, req->rest, payload);

    Trace.printf("Submitting command <%s> for handing\n", req->cmd);
 
    for (std::list<ACSecurityHandler *>::iterator  it =_security_handlers.begin();
         it!=_security_handlers.end();
         ++it)
    {
        cmd_result_t r = (*it)->handle_cmd(req);
        if (r == CMD_CLAIMED) {
	    Trace.printf("handled by sec handler %s\n", (*it)->name());
            goto _done;
	};
    };
    
    Trace.printf("Callback: \tV=%s\n\tB=%s\n\tC=<%s>\n\tP=<%s>\n\tR=<%s>\n\tP=<%s>\n\n", 
	req->version, req->beat, req->cmd, req->payload, req->rest, payload);

    if (_command_callback) {
       cmd_result_t t = _command_callback(req->cmd, req->rest);
       if (t == CMD_CLAIMED) {
	    Trace.printf("handled by callback\n");
            goto _done;
       }
    };

    for (std::list<ACBase *>::iterator it = _handlers.begin();
         it != _handlers.end();
         ++it)
    {
        cmd_result_t r = (*it)->handle_cmd(req);
        if (r == CMD_CLAIMED) {
	    Trace.printf("handled by plain handler %s\n", (*it)->name());
            goto _done;
	};
    }
    
    if (handle_cmd(req) == CMD_CLAIMED)
        goto _done;
 
    Log.printf("Command %s ignored.\n", req->cmd); 
_done:
    delete req;
    return;
}

