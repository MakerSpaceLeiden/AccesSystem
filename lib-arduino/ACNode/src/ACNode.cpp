#include <ACNode.h>
#include "ConfigPortal.h"
#include <Cache.h>
#include <EEPROM.h>

// Sort of a fake singleton to overcome callback
// limits in MQTT callback and elsewhere.
//
ACNode * _acnode;

#ifdef HAS_MSL
MSL msl = MSL();    // protocol doors (private LAN)
#endif

#ifdef HAS_SIG1
SIG1 sig1 = SIG1(); // protocol machines 20015 (HMAC)
#endif

#ifdef HAS_SIG2
SIG2 sig2 = SIG2();
#endif

#ifdef ESP32
#include <WiFi.h>
#include <ETH.h>

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
#endif

#include <TelnetSerialStream.h>
TelnetSerialStream telnetSerialStream = TelnetSerialStream();

#include <WebSerialStream.h>
WebSerialStream  webSerialStream = WebSerialStream();

#include <MqttlogStream.h>

MqttStream  * mqttlogStream;

#ifdef SYSLOG_HOST
#include <SyslogStream.h>
SyslogStream syslogStream = SyslogStream();
#endif

beat_t beatCounter = 0;      // My own timestamp - manually kept due to SPI timing issues.

void ACNode::set_mqtt_host(const char *p) { 
	strncpy(mqtt_server,p, sizeof(mqtt_server)); 
};
void ACNode::set_mqtt_port(uint16_t p)  { mqtt_port = p; };
void ACNode::set_mqtt_prefix(const char *p)  { 
	strncpy(mqtt_topic_prefix,p, sizeof(mqtt_topic_prefix)); 
}
void ACNode::set_mqtt_log(const char *p)  { strncpy(logpath,p, sizeof(logpath)); };
void ACNode::set_moi(const char *p)  { strncpy(moi,p, sizeof(moi)); };
void ACNode::set_machine(const char *p)  { strncpy(machine,p, sizeof(machine)); };
void ACNode::set_master(const char *p)  { strncpy(master,p, sizeof(master)); };

void ACNode::pop() {
    strncpy(mqtt_server, MQTT_SERVER, sizeof(mqtt_server));
    mqtt_port = MQTT_DEFAULT_PORT;
    _report_period = REPORT_PERIOD;

    moi[0] = 0;
    if (machine == NULL || machine[0] == 0)
    	strncpy(machine, String("node-" + chipId() ).c_str(), sizeof(machine));

    strncpy(mqtt_topic_prefix, MQTT_TOPIC_PREFIX, sizeof(mqtt_topic_prefix));
    strncpy(master, MQTT_TOPIC_MASTER, sizeof(master));
    strncpy(logpath, MQTT_TOPIC_LOG, sizeof(logpath));

    // It is safe to start logging early - as these won't emit anyting until
    // the network is known to be up.
    //
    Log.addPrintStream(std::make_shared<TelnetSerialStream>(telnetSerialStream));
    Log.addPrintStream(std::make_shared<WebSerialStream>(webSerialStream));
#ifdef SYSLOG_HOST
    Log.addPrintStream(std::make_shared<SyslogStream>(syslogStream));
#endif
};

IPAddress ACNode::localIP() {
#ifdef ESP32
        if (_wired)
            return ETH.localIP();
        else
#endif
        return WiFi.localIP();
};

String ACNode::macAddressString() {
#ifdef ESP32
         if (_wired)
            return ETH.macAddress();
         else
#endif
        return WiFi.macAddress();
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

void ACNode::set_log_destinations(unsigned int destinations) {
// LOG_SERIAL     LOG_TELNET    LOG_SYSLOG   LOG_WEBBROWSER  LOG_MQTT     
}
void ACNode::set_debug_destinations(unsigned int destinations) {
}

bool ACNode::isConnected() {
#ifdef ESP32
    if (_wired)
        return eth_connected();
#endif
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

void ACNode::begin(eth_board_t board /* default is BOARD_AART */, uint8_t clear_button)
{
    if (!*machine)  
	strncpy(machine, "unset-machine-name", sizeof(machine));

    if (!*moi)  
	strncpy(moi, machine, sizeof(moi));

#define X { Serial.printf("Halting at %s:%d\n", __FILE__, __LINE__); delay(5000); }

#if 0
    if (_debug)
        debugFlash();
#endif

    checkClearEEPromAndCacheButtonPressed(clear_button);

#ifdef ESP32 
    if (_wired)  {
       WiFi.onEvent(WiFiEvent);
    }
#endif
#if 0
       switch(board) {
       case BOARD_NG:
         ETH.begin(ETH_PHY_ADDR, ETH_PHY_RESET, ETH_PHY_MDC, ETH_PHY_MDIO, ETH_PHY_LAN8720, ETH_CLOCK_GPIO17_OUT);
         break;
       case BOARD_OLIMEX:
         ETH.begin(ETH_PHY_ADDR, 12 /* power */, ETH_PHY_MDC, ETH_PHY_MDIO, ETH_PHY_LAN8720, ETH_CLOCK_GPIO17_OUT);
         break;
       case BOARD_AART:
         ETH.begin(1 /* address */, 17 /* power */, ETH_PHY_MDC, ETH_PHY_MDIO, ETH_PHY_LAN8720, ETH_CLK_MODE);
         break;
       default:
         ETH.begin();
      }
#endif

  
#ifdef CONFIGAP
    configBegin();
    
    // Go into Config AP mode if the orange button is pressed
    // just post powerup -- or if we have an issue loading the
    // config.
    //
    static int debounce = 0;
    while (xdigitalRead(PUSHBUTTON) == 0 && debounce < 5) {
        debounce++;
        delay(5);
    };
    if (debounce >= 5 || configLoad() == 0)  {
        configPortal();
    }
#endif
    
    if (_wired) {
        Log.println("Wired mode");
        // WiFi.mode(WIFI_STA);
    } else
    if (_ssid) {
        Log.printf("Starting up wifi (hardcoded SSID <%s>)\n", _ssid);
        WiFi.begin(_ssid, _ssid_passwd);
    } else {
        Log.println("Staring wifi auto connect.");
        WiFiManager wifiManager;
        wifiManager.autoConnect();
    };
    
    // Try up to del seconds to get a WiFi connection; and if that fails; reboot
    // with a bit of a delay.
    //
    const int del = 10; // seconds.
    unsigned long start = millis();
    Debug.print("Connecting..");
    while (!isConnected() && (millis() - start < del * 1000)) {
        delay(500);
	Debug.print(".");
    };
    Debug.println("Connected.");
    
    if (!_wired && !isConnected()) {
        // Log.printf("No connection after %d seconds (ssid=%s). Going into config portal (debug mode);.\n", del, WiFi.SSID().c_str());
        // configPortal();
        Log.printf("No connection after %d seconds (ssid=%s). Rebooting.\n", del, WiFi.SSID().c_str());
        Log.println("Rebooting...");
        delay(1000);
        ESP.restart();
    }
    if(_ssid)
        Log.printf("Wifi connected to <%s>\n", WiFi.SSID().c_str());
   
    Log.print(moi); Log.print(" "); Log.println(localIP());

    MDNS.begin(moi);
    Log.println("MDNS Responder started");

    _espClient = WiFiClient();
    _client = PubSubClient(_espClient);

    char buff[256];
    snprintf(buff, sizeof(buff), "%s/%s/%s", mqtt_topic_prefix, logpath, moi);
    mqttlogStream = new MqttStream(&_client, buff);

    Log.addPrintStream(std::make_shared<MqttStream>(*mqttlogStream));
    configureMQTT();
    reconnectMQTT();
    mqttLoop();

#ifdef CONFIGAP
    configBegin();
#endif

    Log.begin();
    Debug.begin(); 

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
            (*it)->begin();
        }
    }

#if TOFU_WIPE_BUTTON
  // secrit reset button that resets TOFU or the shared
  // secret.
  if (xdigitalRead(TOFU_WIPE_BUTTON) == LOW) {
    extern void wipe_eeprom();
    Log.println("Wiped EEPROM with crypto stuff (SW1 pressed)");
    wipe_eeprom();
  };
#endif

  prepareCache(false);  
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

void ACNode::request_approval(const char * tag, const char * operation, const char * target, bool useCacheOk) { 
	if (tag == NULL) {
		Log.println("invalid tag==NULL passed, approval request not sent");
		return;
	};
 	if (operation == NULL) 
		operation = "energize";

	if (target == NULL) 
		target = machine;

        strncpy(_lasttag, tag, sizeof(_lasttag));
        // Shortcircuit if permitted. Otherwise do the real thing. Note that our cache is primitive
        // just tags - not commands or node/devices.
	if (_approved_callback && useCacheOk && checkCache(_lasttag, beatCounter)) {
                _approved_callback(machine);
	};
       
	char * tmp = (char *)malloc(MAX_MSG);
        char * buff = (char *)malloc(MAX_MSG);
	if (!tmp || !buff) {
		Log.println("Out of memory during cloacking");
		goto _return_request_approval;
		return;
	};

        // We need to copy this - as cloak will overwrite this in place.
        // todo - redesing to be more embedded friendly.
	strncpy(tmp, tag, MAX_MSG);
	if (!(cloak(tmp))) {
		Log.println("Coud not cloak the tag, approval request not sent");
		goto _return_request_approval;
		return;
	};

	Debug.printf("Requesting approval for %s at node %s on machine %s by tag %s\n", 
		operation ? operation : "<null>", moi ? moi: "<null>", operation ? operation : "<null>", tag ? "*****" : "<null>");

	snprintf(buff,MAX_MSG,"%s %s %s %s", operation, moi, target, tmp);

        _lastSwipe = beatCounter;
        _reqs++;
	send(NULL,buff);

_return_request_approval:
	if (tmp) free(tmp);
        if (buff) free(buff);
	return;
};

float loopRate = 0;

void ACNode::report(JsonObject & out) {
		out[ "node" ] = moi;
		out[ "machine" ] = machine;

		out[ "maxMqtt" ] = MAX_MSG;

		char chipstr[30]; strncpy(chipstr,chipId().c_str(),sizeof(chipstr));
		out[ "id" ] = chipstr;
		char ipstr[30]; strncpy(ipstr, String(localIP().toString()).c_str(),sizeof(ipstr));
                out[ "ip" ] = ipstr;
                out[ "net" ] = _wired ? "UTP" : "WiFi";
 		char macstr[30]; strncpy(macstr, macAddressString().c_str(),sizeof(macstr));
  		out[ "mac" ] = macstr;

		out[ "beat" ] = beatCounter;

		if (beatCounter > 1542275849 && _start_beat == 0)
			_start_beat  = beatCounter;
		else 
		if (_start_beat)
			out[ "alive-uptime" ] = beatCounter - _start_beat;

		out[ "approve" ] = _approve;
		out[ "deny" ] = _deny;
		out[ "requests" ] = _reqs;
#ifdef ESP32
		out[ "cache_hit" ] =  cacheHit;
		out[ "cache_miss" ] =  cacheMiss;
		out[ "cache_purge" ] =  cachePurge;
		out[ "cache_update" ] =  cacheUpdate;
#endif

		out[ "mqtt_reconnects" ] = _mqtt_reconnects;

		out["loop_rate"] = loopRate;
#ifdef ESP32
           	out["coreTemp"]  = coreTemp(); 
#endif
		out["heap_free"] = ESP.getFreeHeap();	

		std::list<ACBase *>::iterator it;
       		for (it =_handlers.begin(); it!=_handlers.end(); ++it) 
        		(*it)->report(out);

		if (_report_callback) 
			_report_callback(out);	
}

void ACNode::loop() {
    {
	static unsigned long last = 0, lastCntr = 0, Cntr = 0;
	Cntr++;
	if (millis() - last > (unsigned long)(30 * 1000)) {
		float rate =  1000. * (Cntr - lastCntr)/(millis() - last) + 0.05;
		loopRate = rate;
		if (rate > 10)
			Debug.printf("Loop rate: %.1f #/second\n", rate);
		else
			Log.printf("Warning: LOW Loop rate: %.1f #/second\n", rate);
		last = millis();
		lastCntr = Cntr;
	}
    }

#if 0
    if (_debug) {
    	static unsigned long last = millis();
	static unsigned long sw1, sw2, tock;
	sw1  += xdigitalRead(SW1_BUTTON);
	sw2  += xdigitalRead(SW1_BUTTON);
	tock ++;
	if (millis() - last > 1000) {
	      Debug.printf("SW1: %d %d SW2: %d %d Relay %d Triac %d\n",
	                xdigitalRead(SW1_BUTTON),
	                abs(tock - sw1),
	                xdigitalRead(SW2_BUTTON),
	                abs(tock - sw2),
      	 	        xigitalRead(RELAY_GPIO),
      	 	        xigitalRead(TRIAC_GPIO)
      	      );
    	      last = millis(); sw1 = sw2 = tock = 0;
   	 }
    }
#endif

    {	static unsigned long last = 0;
	if (millis() - last > _report_period) {
		last = millis();

                DynamicJsonDocument jsonDoc(JSON_OBJECT_SIZE(50) + 2000);
                JsonObject out = jsonDoc.to<JsonObject>();
		report(out);

        String buff;
        serializeJson(jsonDoc, buff);
//        if (buff.length() > MAX_MSG) buff = buff.substring(0, MAX_MSG);
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
	    if (_connect_callback) 
	            _connect_callback();
	} else {
	    if (_disconnect_callback) 
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
}

ACBase::cmd_result_t ACNode::handle_cmd(ACRequest * req)
{
    if (!strncmp("ping", req->cmd, 4)) {
        char buff[MAX_TOKEN_LEN*2];
        IPAddress myIp = localIP();
        
        snprintf(buff, sizeof(buff), "ack %s %s %d.%d.%d.%d", master, moi, myIp[0], myIp[1], myIp[2], myIp[3]);
        send(NULL, buff);
	Debug.println("replied on the pick with an ack.");
        return ACNode::CMD_CLAIMED;
    };
    if (!strcmp("clearcache", req->cmd)) {
	Log.println("Command received to clear the cache");
        wipeCache();
        return ACNode::CMD_CLAIMED;
    }
    if (!strcmp("unauthorize", req->cmd)) {
         char tmp[MAX_MSG], *p = tmp;
         strncpy(tmp, req->rest, sizeof(tmp));
         SEP(tag, "No tag in unauthorize command", ACNode::CMD_CLAIMED)
         unsetCache(req->rest);
         return ACNode::CMD_CLAIMED;
    };

    bool app = ((strcasecmp("approved",req->cmd)==0) || (strcasecmp("open",req->cmd)==0));
    bool den = (strcasecmp("denied", req->cmd) == 0);
    // if (den) { den = false; app = true; };

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

      if (bc != _lastSwipe && bc != _lastSwipe+1) {
          Log.printf("Out of order energize/denied command received - ignored (got %lu, expected %lu)\n", bc, _lastSwipe);
          return ACNode::CMD_CLAIMED;
      };


      if (app) {
         setCache(_lasttag, app, (unsigned long) beatCounter);
         Log.printf("Received OK to power on %s\n", machine);
         if (_approved_callback) {
		_approved_callback(machine);
        	return ACNode::CMD_CLAIMED;
         };
     } else {
         unsetCache(_lasttag);
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
#if defined(HAS_SIG1) || defined (HAS_SIG2) || defined (HAS_MSL)
        Log.println("Unrecognized payload. Ignoring.");
#endif
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

void ACNode::delayedReboot() {
   static int warn_counter = 0;
   static unsigned long last = 0;

   if (millis() - last < 1000) 
       return;

   if (warn_counter > 5) {
        Log.println("Forced reboot NOW");
        ESP.restart();
   };

   char buff[255];
   snprintf(buff,MAX_MSG,"Countdown to forced reboot: %d", 5 - warn_counter);

   Log.println(buff);

   last = millis();
   warn_counter ++;
}

 #ifdef HAS_SIG2
 void ACNode::add_trusted_node(const char *node) {
        sig2.add_trusted_node(node);
 }
 #endif

void ACNode::checkClearEEPromAndCacheButtonPressed(uint8_t button) {
  unsigned long ButtonPressedTime;
  unsigned long currentSecs;
  unsigned long prevSecs;
  bool firstTime = true;

  if (button >254)
	return;

  // check button pressed
  pinMode(button, INPUT);
  // check if button is pressed for at least 3 s
  Log.println("Checking if the button is pressed for clearing EEProm and cache");
  ButtonPressedTime = millis();
  prevSecs = MAX_WAIT_TIME_BUTTON_PRESSED / 1000;
  Log.print(prevSecs);
  Log.print(" s");
  while (digitalRead(button) == CLEAR_EEPROM_AND_CACHE_BUTTON_PRESSED) {
    if ((millis() - ButtonPressedTime) >= MAX_WAIT_TIME_BUTTON_PRESSED) {
      if (firstTime == true) {
        Log.print("\rPlease release button");
        firstTime = false;
      }
    } else {
      currentSecs = (MAX_WAIT_TIME_BUTTON_PRESSED - millis()) / 1000;
      if ((currentSecs != prevSecs) && (currentSecs >= 0)) {
        Log.print("\r");
        Log.print(currentSecs);
        Log.print(" s");
        prevSecs = currentSecs;
      }
    }
  }
  if (millis() - ButtonPressedTime >= MAX_WAIT_TIME_BUTTON_PRESSED) {
    Log.print("\rButton for clearing EEProm and cache was pressed for more than ");
    Log.print(MAX_WAIT_TIME_BUTTON_PRESSED / 1000);
    Log.println(" s, EEProm and Cache will be cleared!");
    // Clear EEPROM
    EEPROM.begin(1024);
    wipe_eeprom();
    Log.println("EEProm cleared!");
    // Clear cache
    prepareCache(true);
    Log.println("Cache cleared!");
    // wait until button is released, than reboot
    while (digitalRead(button) == CLEAR_EEPROM_AND_CACHE_BUTTON_PRESSED) {
      // do nothing here
    }
    Log.println("Node will be restarted");
    // restart node
    ESP.restart();
  } else {
    Log.println("\rButton was not (or not long enough) pressed to clear EEProm and cache");
  }
}


