#ifdef ESP32
#include <WiFi.h>
#include <ETH.h>
#endif
#include "SIG/Cache.h"
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

            strncpy((char *)_lasttag, tag, sizeof(_lasttag));
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
#ifdef ESP32
    out[ "cache_hit" ] =  cacheHit;
    out[ "cache_miss" ] =  cacheMiss;
    out[ "cache_purge" ] =  cachePurge;
    out[ "cache_update" ] =  cacheUpdate;
#endif
}

void ACNode::checkClearEEPromAndCacheButtonPressed(uint8_t button) {
    const unsigned long prevSecs = MAX_WAIT_TIME_BUTTON_PRESSED / 1000;
    
    if (button == 255)
        return;
    
    // check button pressed
    pinMode(button, button);
    
    // check if button is pressed for at least 3 s
    Log.printf("Hold button for %d seconds to clearing EEProm and cache.\n", prevSecs);
    
    if (xdigitalRead(button) != CLEAR_EEPROM_AND_CACHE_BUTTON_PRESSED)
        return;
    
    unsigned long _start = millis();
    while (xdigitalRead(button) == CLEAR_EEPROM_AND_CACHE_BUTTON_PRESSED) {
        if ((millis() - _start) > MAX_WAIT_TIME_BUTTON_PRESSED) {
            // Clear EEPROM
            EEPROM.begin(1024);
            wipe_eeprom();
            Log.println("EEProm cleared!");
            
            // Clear cache
            prepareCache(true);
            Log.println("Cache cleared!");
            
            Log.println("Node rebooting");
            ESP.restart();
        };
    }
    Log.println("Button was not (or not long enough) pressed to clear EEProm and cache\n");
    return;
}

// We're having a bit of an issue with publishing within/near the reconnect and mqtt callback. So we
// queue the message up - to have them send in the runloop; much later. We also do the signing that
// late - as this also seeems to occasionally hit some (stackdepth?) limit.
//
typedef struct publish_rec {
    char * topic;
    char * payload;
    struct publish_rec * nxt;
    bool raw;
} publish_rec_t;

publish_rec_t *publish_queue = NULL;

void ACNode::send(const char * topic, const char * payload, bool _raw) {
    char _topic[MAX_TOPIC];
    
    if (topic == NULL) {
        snprintf(_topic, sizeof(_topic), "%s/%s/%s", mqtt_topic_prefix, master, ACNode::moi);
        topic = _topic;
    }
    else if (index(topic,'/') == NULL) {
        snprintf(_topic, sizeof(_topic), "%s/%s/%s", mqtt_topic_prefix, ACNode::moi, topic);
        topic = _topic;
    }
    
    //    Serial.printf("send('%s','%s',%d)\n", topic ? topic : "<null>", payload ? payload : "<null>" , _raw);
    
    publish_rec_t * rec = (publish_rec_t *)malloc(sizeof(publish_rec_t));
    if (rec) {
        rec->topic = strdup(topic);
        rec->payload = strdup(payload);
        rec->raw = _raw;
        rec->nxt = NULL;
    }
    
    if (!rec || !(rec->topic) || !(rec->payload)) {
        Serial.println("Out of memory");
#ifdef DEBUG
        // Throw a core dump for debugging/GDBSTUB_H purposes.
        *((int*)0) = 0;
#endif
        ESP.restart();
    };
    // We append at the very end -- this preserves order -and- allows
    // us to add things to the queue while in something works on it.
    //
    publish_rec_t ** p = &publish_queue;
    int i = 0;
    while (*p) {
        p = &(*p)->nxt;
        i++;
    };
    *p = rec;
    
    //    Serial.printf("Queued at # %d\n",i);
}

void ACNode::reconnectMQTT() {
    super::reconnectMQTT();
    
    char topic[MAX_TOPIC];

    snprintf(topic, sizeof(topic), "%s/%s/%s", mqtt_topic_prefix, moi, master);
    _client.subscribe(topic);
    Debug.print("master to me -- subscribed to ");
    Debug.println(topic);
    
    snprintf(topic, sizeof(topic), "%s/%s/%s", mqtt_topic_prefix, master, master);
    _client.subscribe(topic);
    Debug.print("master bcast -- Subscribed to ");
    Debug.println(topic);
    
    send_helo(NULL);
}

void ACNode::send_helo(char * token) {
    char topic[MAX_TOPIC];
    snprintf(topic, sizeof(topic), "%s/%s/%s", mqtt_topic_prefix, moi, master);
    
    ACRequest * req = new ACRequest(topic, token ? token : "announce");
    
    bool canBeSent = false;
    
    ACSecurityHandler::acauth_results r = ACSecurityHandler::FAIL;
    for (std::list<ACSecurityHandler *>::iterator it =_security_handlers.begin();
         it!=_security_handlers.end() && r != ACSecurityHandler::OK;
         ++it)
    {
        r = (*it)->helo(req);
        switch(r) {
            case ACSecurityHandler::DECLINE:
                break;
            case ACSecurityHandler::PASS:
            case ACSecurityHandler::OK:
                canBeSent = true;
                break;
            case ACSecurityHandler::FAIL:
            default:
                Log.printf("Failing HELO on (%s) - failing.\n", (*it)->name());
                return;
                break;
        }
    }
    // at least someone should have touched it.
    if (canBeSent) {
        Debug.printf("Send from reconnect: %s\n", req->payload);
        send(NULL, req->payload);
    } else {
        Debug.printf("No helo yet sent; not enough stack up.\n");
    }
}


void mqtt_callback(char* topic, byte * payload_theirs, unsigned int length) {
    char payload[MAX_MSG], *q = payload;
    
    if (length >= sizeof(payload))
        length = sizeof(payload)-1;
    
    for(unsigned char *p = payload_theirs; length > 0 && *p; p++, length--) {
        if (*p >= 32 && *p < 128) {
            *q++ = *p;
        };
    };
    *q = 0;
    
    _acnodebase->process(topic, payload);
}

void ACNode::mqttLoop() {
    ACNodeBase::mqttLoop();
    
    if (!publish_queue)
        return;
    
    // Publish just once. Rely on the loop to return
    // here quickly.
    //
    publish_rec_t * rec = publish_queue;
    
    //    Serial.printf("Picking from queu: <%s>\n", rec->payload);
    
    ACRequest * reqOut = new ACRequest();
    if (!reqOut) {
        Serial.println("Out of memory. Rebooting");
        delay(1000);
        ESP.restart();
    };
    
    strncpy(reqOut->topic, rec->topic, sizeof(reqOut->topic));
    strncpy(reqOut->payload, rec->payload, sizeof(reqOut->payload));
    
    // We are runing in reverse order. As we need to
    // `wrap things' back up.
    //
    std::list<ACSecurityHandler *>::reverse_iterator it;
    ACSecurityHandler::acauth_results r = ACSecurityHandler::FAIL;
    
    if (rec->raw == false) {
        for (it =_security_handlers.rbegin();
             it!=_security_handlers.rend() && r != ACSecurityHandler::OK;
             ++it) {
            // Debug.printf("PRE  %s: %s %s\n", (*it)->name(), reqOut->payload, reqOut->rest);
            r = (*it)->secure(reqOut);
            if (r == ACSecurityHandler::FAIL) {
                Log.printf("Adding signature to outbound failed (%s). Aborting.\n", (*it)->name());
                Log.printf("\t%s\n\t%s\n", reqOut->topic, reqOut->payload);
                goto _done_without_send;
            };
            // Debug.printf("POST %s: %s %s\n", (*it)->name(), reqOut->payload, reqOut->rest);
        }
    }
    
    if (!rec->raw)
        Debug.printf("[%s]%s>>: %s\n", reqOut->topic, rec->raw ? "r" : " ", reqOut->payload);
    
    _client.publish(reqOut->topic, reqOut->payload);
    
_done_without_send:
    delete reqOut;
    publish_queue = rec->nxt;
    free(rec->topic);
    free(rec->payload);
    free(rec);
}
