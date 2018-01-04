#include <ACNode.h>

#if MQTT_MAX_PACKET_SIZE < 256
#error "You will need to increase te MQTT_MAX_PACKET_SIZE size a bit in PubSubClient.h"
#endif


char mqtt_server[34] = "not-yet-cnf-mqtt";
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

void send(const char * topic, const char * payload) {
	_acnode.send(topic,payload);
}

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
#if 0
  static char buff[10]; snprintf(buff, sizeof(buff), "Error: %d", state);
  return buff;
#endif

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
}

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
  return NULL;
}

void ACNode::reconnectMQTT() {
  Debug.printf("Attempting MQTT connection to %s:%d (MQTT State : %s)\n",
               mqtt_server, mqtt_port, state2str(_client.state()));

  if (!_client.connect(moi)) {
    Log.print("failed : ");
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
  char payload[MAX_MSG];


  memcpy(payload, payload_theirs, length);
  payload[length] = 0;

  Debug.print("["); Debug.print(topic); Debug.print("] <<: ");
  Debug.print((char *)payload);
  Debug.println();

  if (length < 6 + 2 * HASH_LENGTH + 1 + 12 + 1) {
    Log.println("Too short - ignoring.");
    return;
  };
  char * p = (char *)payload;

  SEP(version, "SIG header",/* void return */);
  SEP(sig, "Signature",/* void return */);

  char signed_payload[MAX_MSG];
  strncpy(signed_payload, p, sizeof(signed_payload));
  SEP(beat, "BEAT",/* void return */);
  char * rest = p;

  if (!strncmp(version, "SIG/1", 5)) {
    if (!hmac_valid(sig, passwd, beat, topic, p)) {
      return;
    }
  } else if (!strncmp(version, "SIG/2", 5)) {
    if (!sig2_verify(beat, sig, signed_payload))
      return;
  } else {
    Log.print("Unknown signature format <"); Log.print(version); Log.println("> - ignoring.");
    return;
  };

  if (!strncmp("announce", rest, 8)) {
    send_helo((char *)"welcome");
    return;
  }

  if (!strncmp("welcome", rest, 7)) {
    return;
  }

  if (!strncmp("beat", rest, 4)) {
    return;
  }

  if (!strncmp("ping", rest, 4)) {
    char buff[MAX_MSG];
    IPAddress myIp = WiFi.localIP();

    snprintf(buff, sizeof(buff), "ack %s %s %d.%d.%d.%d", master, moi, myIp[0], myIp[1], myIp[2], myIp[3]);
    send(NULL, buff);
    return;
  }

#if 0
  if (!strncmp("state", rest, 4)) {
    char buff[MAX_MSG];
    snprintf(buff, sizeof(buff), "state %d %s", machinestate, machinestateName[machinestate]);
    send(NULL, buff);
    return;
  }

#ifdef RFID
  unsigned long  b = atol(beat);
  if (handleRFID(b, rest)) {
    machinestate = POWERED;
    return;
  };
#endif

  if (!strncmp("approved", rest, 8) || !strncmp("energize", rest, 8)) {
    machinestate = POWERED;
    return;
  }

  if (!strncmp("denied", rest, 6) || !strncmp("unknown", rest, 7)) {
    Log.println("Flash LEDS");
    setRedLED(LED_FAST);
    machinestate = PAUSED_AFTER_ERROR;
    return;
  };

  if (!strcmp("outoforder", rest)) {
    machinestate = OUTOFORDER;
    send(NULL, "event outoforder");
    return;
  }
#endif

  Log.printf("Do not know what to do with <%s>, ignoring.\n", rest);
  return;
}

void ACNode::mqttLoop() {
  static unsigned long last_mqtt_connect_try = 0;

  _client.loop();

  if (!_client.connected()) {
    // report transient error ? Which ? And how often ?
    if (millis() - last_mqtt_connect_try > 10000 || last_mqtt_connect_try == 0) {
      reconnectMQTT();
      last_mqtt_connect_try = millis();
    }
    return;
  };

  if (!publish_queue)
    return;

  // Publish just once. Rely on the loop to return
  // here quickly.
  //
  publish_rec_t * rec = publish_queue;
  char msg[ MAX_MSG];
  char beat[MAX_BEAT];

  snprintf(beat, sizeof(beat), BEATFORMAT, beatCounter);

  if (sig2_active()) {
    char tosign[MAX_MSG];
    snprintf(tosign, sizeof(tosign), "%s %s", beat, rec->payload);
    sig2_sign(msg, sizeof(msg), tosign);
  } else {
    hmac_sign(msg, sizeof(msg), beat, rec->payload);
  }

  _client.publish(rec->topic, msg);

  publish_queue = rec->nxt;
  free(rec->topic);
  free(rec->payload);
  free(rec);
}

