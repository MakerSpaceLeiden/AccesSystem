#include <ACBase.h>
#include <ACNode.h>
#include "ConfigPortal.h"

// Sort of a fake singleton to overcome callback
// limits in MQTT callback and elsewhere.
//
ACNode * _acnode;
ACLog Log;
ACLog Debug;

beat_t beatCounter = 0;      // My own timestamp - manually kept due to SPI timing issues.

ACNode::ACNode(bool wired) :
_ssid(NULL), _ssid_passwd(NULL), _wired(wired)
{
    _acnode = this;
}

ACNode::ACNode(const char * ssid , const char * ssid_passwd ) :
_ssid(ssid), _ssid_passwd(ssid_passwd), _wired(false)
{
    _acnode = this;
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
        Debug.println("starting up ethernet");
        eth_setup();
    };
    
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
        Debug.println("starting up ethernet ");
        WiFi.mode(WIFI_STA);
    };
    if (_ssid) {
        Serial.println("starting up wifi (fixed SSID)");
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
    
    Log.print("IP address: ");
    Log.println(WiFi.localIP());
    
    _espClient = WiFiClient();
    _client = PubSubClient(_espClient);
#if 0
    _ethClient = EthernetClient();
    _client = PubSubClient(ethClient);
#endif
    
    configBegin();
    configureMQTT();
    
    if (_debug)
        debugListFS("/");

    // Note that this will also run the security and ohter handlers; see
    // addSecurityHandler().
    //
    {
        std::list<ACBase *>::iterator it;
        for (it =_handlers.begin(); it!=_handlers.end(); ++it) {
            (*it)->begin();
        }
    }
}

char * ACNode::cloak(char tag[MAX_MSG]  ) {
    ACRequest q = ACRequest();
    strncpy(q.tag, tag, sizeof(q.tag));
    std::list<ACSecurityHandler *>::iterator it;
    for (it =_security_handlers.begin(); it!=_security_handlers.end(); ++it) {
        int r = (*it)->cloak(&q);
        if (r != 0)
            return NULL;
    }
    strncpy(tag, q.tag, MAX_MSG);
    return tag;
}

void ACNode::loop() {
    // XX to hook into a callback of the ethernet/wifi
    // once we figure out how we can get this from the wifi.
    //
    static bool lastconnectedstate = false;
    bool connectedstate = isConnected();
    if (lastconnectedstate != connectedstate) {
        if (connectedstate)
            _connect_callback();
        else
            _disconnect_callback();
        lastconnectedstate = connectedstate;
    };
    
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
    if (!strncmp("welcome", req->cmd, 7)) {
        return ACNode::CMD_CLAIMED;
    }
    
    if (!strncmp("ping", req->cmd, 4)) {
        char buff[MAX_MSG];
        IPAddress myIp = WiFi.localIP();
        
        snprintf(buff, sizeof(buff), "ack %s %s %d.%d.%d.%d", master, moi, myIp[0], myIp[1], myIp[2], myIp[3]);
        send(NULL, buff);
        return ACNode::CMD_CLAIMED;
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
    size_t l = 0;
    char * endOfCmd = NULL;
    
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
            case ACSecurityHandler::OK:
                Debug.printf("OK payload with %s signature - handling.\n", (*it)->name());
                break;
            case ACSecurityHandler::FAIL:
            default:
                Log.printf("Invalid/unknown payload or signature (%s) - failing.\n", (*it)->name());
                goto _done;
                break;
        }
    }
    if (r != ACSecurityHandler::OK) {
        Log.println("Unrecognized payload. Ignoring.");
        goto _done;
    }
    
    endOfCmd = index(req->rest, ' ');
    if (endOfCmd) {
        l = endOfCmd - req->rest;
        if (l >= sizeof(req->cmd))
            l = sizeof(req->cmd);
        strncpy(req->cmd, req->rest, l);
        strncpy(req->rest, req->rest + l +1, sizeof(req->rest));
    } else {
        strncpy(req->cmd,req->rest, sizeof(req->cmd));
        req->rest[0] = '\0';
    };
    
    for (std::list<ACSecurityHandler *>::iterator  it =_security_handlers.begin();
         it!=_security_handlers.end();
         ++it)
    {
        cmd_result_t r = (*it)->handle_cmd(req);
        if (r == CMD_CLAIMED)
            goto _done;
    };
    
    for (std::list<ACBase *>::iterator it = _handlers.begin();
         it != _handlers.end();
         ++it)
    {
        cmd_result_t r = (*it)->handle_cmd(req);
        if (r == CMD_CLAIMED)
            goto _done;
    }
    
    // Try my own default things.
    //
    if (handle_cmd(req) == CMD_CLAIMED)
        goto _done;
    
    Log.printf("Do not know what to do with <%s>, ignoring.\n", payload);
    
_done:
    delete req;
    return;
}

