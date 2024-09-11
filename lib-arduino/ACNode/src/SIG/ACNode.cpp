#ifdef ESP32
#include <WiFi.h>
#include <ETH.h>
#endif

#include "ACNode.h"
#include "ConfigPortal.h"
#include "Cache.h"
#include "EEPROM.h"

// Section: foo
/// SIG based auth
///
///
#ifdef HAS_MSL
MSL msl = MSL();    // protocol doors (private LAN)
#endif

#ifdef HAS_SIG1
SIG1 sig1 = SIG1(); // protocol machines 20015 (HMAC)
#endif

#ifdef HAS_SIG2
SIG2 sig2 = SIG2();
#endif

#if defined(HAS_SIG1) || defined (HAS_SIG2) || defined (HAS_MSL)
// Sort of a fake singleton to overcome callback
// limits in MQTT callback and elsewhere.
//
// For use in callbacks that are from plain C
ACNode *_acnode;

void send(const char * topic, const char * payload) {
    if(_acnode)
        _acnode->send(topic,payload);
}
#endif

ACNode::ACNode(const char * m, bool wired, acnode_proto_t proto) : ACNodeBase(m, wired), _proto(proto)
{
    _acnode = this;
    pop();
}

ACNode::ACNode(const char *m, const char * ssid , const char * ssid_passwd, acnode_proto_t proto ) : ACNodeBase(m,ssid,ssid_passwd), _proto(proto)
{
    _acnode = this;
    pop();
}

void ACNode::pop() {
}

void ACNode::addSecurityHandler(ACSecurityHandler * handler) {
    _security_handlers.insert(_security_handlers.end(), handler);
    
    // Some handlers need a begin or loop maintenance cycle - so we
    // also add these to the normal loop.
    addHandler(handler);
}

void ACNode::begin(eth_board_t board /* default is BOARD_AART */, uint8_t clear_button) {
    super::_begin(board, clear_button);
    
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
    _complete_begin(clear_button);

    Log.println("Listening on MQTT bus");
    _client.setCallback(mqtt_callback);
};

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

#ifdef HAS_SIG2
void ACNode::add_trusted_node(const char *node) {
    sig2.add_trusted_node(node);
}
#endif

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
}

void ACNode::report(JsonObject & out) {
    if (_start_beat == 0)
        if (beatCounter > 50000)
            _start_beat =beatCounter +  millis()/1000;
    
    ACBase::report(out);

    if (_start_beat)
        out[ "alive-uptime" ] = uptimeInSeconds();

    out[ "beat" ] = beatCounter;
}
