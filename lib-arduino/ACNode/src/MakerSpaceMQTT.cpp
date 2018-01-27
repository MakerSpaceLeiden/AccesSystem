#include <ACNode.h>

#if MQTT_MAX_PACKET_SIZE < 256
#error "You will need to increase te MQTT_MAX_PACKET_SIZE size a bit in PubSubClient.h"
#endif


char mqtt_server[34] = "space.vijn.org";
uint16_t mqtt_port = MQTT_DEFAULT_PORT;

// MQTT topics are constructed from <prefix> / <dest> / <sender>
//
char mqtt_topic_prefix[MAX_TOPIC] = "test";
char moi[MAX_NAME] = "exhaustnode";    // Name of the sender
char machine[MAX_NAME] = "fan";
char master[MAX_NAME] = "master";    // Destination for commands
char logpath[MAX_NAME] = "log";       // Destination for human readable text/logging info.
char passwd[MAX_NAME] = "none-set";


// We're having a bit of an issue with publishing within/near the reconnect and mqtt callback. So we
// queue the message up - to have them send in the runloop; much later. We also do the signing that
// late - as this also seeems to occasionally hit some (stackdepth?) limit.
//
typedef struct publish_rec {
    char * topic;
    char * payload;
    struct publish_rec * nxt;
} publish_rec_t;
publish_rec_t *publish_queue = NULL;


void ACNode::send(const char * topic, const char * payload) {
    char _topic[MAX_TOPIC];
    
    if (topic == NULL) {
        snprintf(_topic, sizeof(_topic), "%s/%s/%s", mqtt_topic_prefix, master, moi);
        topic = _topic;
    }
    
    publish_rec_t * rec = (publish_rec_t *)malloc(sizeof(publish_rec_t));
    if (rec) {
        rec->topic = strdup(topic);
        rec->payload = strdup(payload);
        rec->nxt = NULL;
    }
    
    if (!rec || !(rec->topic) || !(rec->payload)) {
        Debug.println("Out of memory");
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
    while (*p) p = &(*p)->nxt;
    *p = rec;
}


const char * state2str(int state) {
#if __ATMEL_8BIT
    static char buff[10]; snprintf(buff, sizeof(buff), "Error: %d", state);
    return buff;
#else
    switch (state) {
        case  /* -4 */ MQTT_CONNECTION_TIMEOUT:
            return "the server didn't respond within the keepalive time";
        case  /* -3 */ MQTT_CONNECTION_LOST :
            return "the network connection was broken";
        case  /* -2 */ MQTT_CONNECT_FAILED :
            return "the network connection failed";
        case  /* -1  */ MQTT_DISCONNECTED :
            return "the client is disconnected (clean)";
        case  /* 0  */ MQTT_CONNECTED :
            return "the client is connected";
        case  /* 1  */ MQTT_CONNECT_BAD_PROTOCOL :
            return "the server doesn't support the requested version of MQTT";
        case  /* 2  */ MQTT_CONNECT_BAD_CLIENT_ID :
            return "the server rejected the client identifier";
        case  /* 3  */ MQTT_CONNECT_UNAVAILABLE :
            return "the server was unable to accept the connection";
        case  /* 4  */ MQTT_CONNECT_BAD_CREDENTIALS :
            return "the username/password were rejected";
        case  /* 5  */ MQTT_CONNECT_UNAUTHORIZED :
            return "the client was not authorized to connect";
        default:
            break;
    }
    return "Unknown MQTT error";
#endif
}

void ACNode::reconnectMQTT() {
    Debug.printf("Attempting MQTT connection to %s:%d (MQTT State : %s)\n",
                 mqtt_server, mqtt_port, state2str(_client.state()));
    
    if (!_client.connect(moi)) {
        Log.print("Reconnect failed : ");
        Log.println(state2str(_client.state()));
    }
    
    Debug.println("(re)connected ");
    
    char topic[MAX_TOPIC];
    snprintf(topic, sizeof(topic), "%s/%s/%s", mqtt_topic_prefix, moi, master);
    _client.subscribe(topic);
    Debug.print("Subscribed to ");
    Debug.println(topic);
    
    snprintf(topic, sizeof(topic), "%s/%s/%s", mqtt_topic_prefix, master, master);
    _client.subscribe(topic);
    Debug.print("Subscribed to ");
    Debug.println(topic);
    
    char buff[MAX_MSG];
    IPAddress myIp = WiFi.localIP();
    snprintf(buff, sizeof(buff), "announce %d.%d.%d.%d", myIp[0], myIp[1], myIp[2], myIp[3]);
    send(topic, buff);
}

void mqtt_callback(char* topic, byte * payload_theirs, unsigned int length);

void ACNode::configureMQTT()  {
    _client.setServer(mqtt_server, mqtt_port);
    _client.setCallback(mqtt_callback);
}

#ifndef ESP32
char * strsepspace(char **p) {
    char *q = *p;
    while (**p && **p != ' ') {
        (*p)++;
    };
    if (**p == ' ') {
        **p = 0;
        (*p)++;
        return q;
    }
    if (*q)
        return q;
    return NULL;
}
#endif

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
    
    _acnode->process(topic, payload);
}

bool ACNode::isUp() {
    return _client.connected();
}

void ACNode::mqttLoop() {
    static unsigned long last_mqtt_connect_try = 0;
    
    if (last_mqtt_connect_try == 0 || !isUp()) {
        // report transient error ? Which ? And how often ?
        if (millis() - last_mqtt_connect_try > 10000 || last_mqtt_connect_try == 0) {
            reconnectMQTT();
            last_mqtt_connect_try = millis();
        }
        return;
    };
    
    _client.loop();
    
    if (!publish_queue)
        return;
    
    // Publish just once. Rely on the loop to return
    // here quickly.
    //
    publish_rec_t * rec = publish_queue;
    char * payload = rec->payload;
    
    // We are runing in reverse order. As we need to
    // `wrap things' back up.
    //
    std::list<ACSecurityHandler>::reverse_iterator it;
    ACSecurityHandler::acauth_results r = ACSecurityHandler::FAIL;
    for (it =_security_handlers.rbegin(); it!=_security_handlers.rend() && r != OK; ++it) {
        char * p = payload;
        r = it->verify(rec->topic, payload, &p);
        if (r == ACSecurityHandler::FAIL) {
            Log.printf("Adding signature to outbound failed (%s). Aborting.\n", it->name);
            goto _done_without_send;
        };
        payload = p;
    }
    _client.publish(rec->topic, payload);
    
_done_without_send:
    publish_queue = rec->nxt;
    free(rec->topic);
    free(rec->payload);
    free(rec);
    
}

