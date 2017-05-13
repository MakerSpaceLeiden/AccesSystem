#include "MakerspaceMQTT.h"
#include <sha256.h>

#if MQTT_MAX_PACKET_SIZE < 256
#error "You will need to increase te MQTT_MAX_PACKET_SIZE size a bit in PubSubClient.h"
#endif

#ifndef _
#define _(x) #x
#endif

ACNode::ACNode() {
  configuration = jsonBuffer.createObject();
  configuration["mqtt_server"] = jsonBuffer.parseObject("{\"value\":\"\",\"size\":34,\"descr\":\"FQDN of MQTT Server\"}");
  configuration["mqtt_port"] = jsonBuffer.parseObject("{\"value\":\"" _(MQTT_DEFAULT_PORT) "\",\"size\":6,\"descr\":\"Port on MQTT Server\",\"type\":\"port\"}");
  configuration["mqtt_topic_prefix"] = jsonBuffer.parseObject("{\"value\":\"test\",\"size\":6,\"descr\":\"Port on MQTT Server\"}");
  configuration["nodename"] = jsonBuffer.parseObject("{\"value\":\"testnode\",\"size\":6,\"descr\":\"Port on MQTT Server\"}");
  configuration["machine"] = jsonBuffer.parseObject("{\"value\":\"machine\",\"size\":6,\"descr\":\"Port on MQTT Server\"}");
  configuration["master"] = jsonBuffer.parseObject("{\"value\":\"testmaster\",\"size\":6,\"descr\":\"Port on MQTT Server\"}");
  configuration["logpath"] = jsonBuffer.parseObject("{\"value\":\"log\",\"size\":6,\"descr\":\"Port on MQTT Server\"}");
  configuration["passwd"] = jsonBuffer.parseObject("{\"value\":\"FooBar\",\"size\":6,\"descr\":\"Port on MQTT Server\",\"type\":\"hide\"}");
}

void ACNode::json_copy_if_valid(JsonObject &newConfig, const char * key, void ** val, size_t length) {
  const char * str = newConfig[ key ];
  const char * pre = configuration[key]["value"];
  str = str ? str : "";

  if (configuration[key].exists("type") && configuration[key]["type"] == "port") {
    int p = atoi(str);
    if (p == 0) p = MQTT_DEFAULT_PORT;
    if (p < 65564) mqtt_port = p;
    *(uint16_t *)val = (uint16_t) p;
    configuration[key][value] = p;
  } else {
    strncpy(*(const char*)val, str, length);
    configuration[key]["value"] = *(const char*)val;
  }
  if (configuration[key].exists("type") && configuration[key]["type"] == "hide")
    str = "****";
  Debug.printf("CNF: %s=\"%s\" ==> \"%s\"\n", key, pre, str);
}

void ACNode::updateConfig(JsonObject &newConfig) {
#define JCI(variable, fieldname) json_copy_if_valid(newConfig, fieldname, &variable, sizeof(variable))
  JCI(mqtt_server, "mqtt_server");
  JCI(nodename, "nodename");
  JCI(mqtt_topic_prefix, "prefix");
  JCI(passwd, "passwd");
  JCI(logpath, "logpath");
  JCI(master, "master");
  JCI(machine, "machine");
  JCI(tmp_port, "mqtt_port");
}

void ACNode::begin() {
  client = Client();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqtt_callback);
}

const char * state2str(int state) {
#ifdef DEBUG
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
      return "the cient is connected";
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
#else
  static char buff[10]; snprintf(buff, sizeof(buff), "Error: %d", state);
  return buff;
#endif
}

// Note - none of below HMAC utility functions is re-entrant/t-safe; they all rely
// on some private static buffers one is not to meddle with in the 'meantime'.
//
const char * hmacToHex(const unsigned char * hmac) {
  static char hex[2 * HASH_LENGTH + 1];
  const char q2c[] = "0123456789abcdef"; // Do not 'uppercase' -- the HMACs are calculated over it - and hence are case sensitive.
  char * p = hex;

  for (int i = 0; i < HASH_LENGTH; i++) {
    *p++ = q2c[hmac[i] >> 4];
    *p++ = q2c[hmac[i] & 15];
  };
  *p++ = 0;

  return hex;
}

const unsigned char * hmacBytes(const char *passwd, const char * beatAsString, const char * topic, const char *payload) {
  // static char hex[2 * HASH_LENGTH + 1];

  Sha256.initHmac((const uint8_t*)passwd, strlen(passwd));
  Sha256.print(beatAsString);
  if (topic && *topic) Sha256.print(topic);
  if (payload && *payload) Sha256.print(payload);

  return Sha256.resultHmac();
}

const char * hmacAsHex(const char *passwd, const char * beatAsString, const char * topic, const char *payload)
{
  const unsigned char * hmac = hmacBytes(passwd, beatAsString, topic, payload);
  return hmacToHex(hmac);
}


void ACNode::send(const char *payload) {
  char msg[ MAX_MSG];
  char beat[MAX_BEAT], topic[MAX_TOPIC];

  snprintf(beat, sizeof(beat), BEATFORMAT, beatCounter);
  snprintf(topic, sizeof(topic), "%s/%s/%s", mqtt_topic_prefix, master, nodename);

  msg[0] = 0;
  snprintf(msg, sizeof(msg), "SIG/1.0 %s %s %s",
           hmacAsHex(passwd, beat, topic, payload),
           beat, payload);

  client.publish(topic, msg);

  Debug.print("Sending ");
  Debug.print(topic);
  Debug.print(": ");
  Debug.println(msg);
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

  Debug.printf("Attempting MQTT connection to %s:%d (State : %s\n",
               mqtt_server, mqtt_port, state2str(client.state()));

  //  client.connect(nodename);
  //  client.subscribe(mqtt_topic_prefix);

  if (client.connect(nodename)) {
    Log.println("(re)connected " BUILD);

    char topic[MAX_TOPIC];
    snprintf(topic, sizeof(topic), "%s/%s/%s", mqtt_topic_prefix, nodename, master);
    client.subscribe(topic);
    Debug.print("Subscribed to ");
    Debug.println(topic);

    snprintf(topic, sizeof(topic), "%s/%s/%s", mqtt_topic_prefix, master, master);
    client.subscribe(topic);
    Debug.print("Subscribed to ");
    Debug.println(topic);

    char buff[MAX_MSG];
    IPAddress myIp = WiFi.localIP();
    snprintf(buff, sizeof(buff), "announce %d.%d.%d.%d", myIp[0], myIp[1], myIp[2], myIp[3]);
    send(buff);
  } else {
    Log.print("failed : ");
    Log.println(state2str(client.state()));
  }
}


void mqtt_callback(char* topic, byte * payload_theirs, unsigned int length) {
  node.handle_command(topic, payload_theirs, length);
}

void ACNode::callback(char* topic, byte * payload_theirs, unsigned int length) {
  char payload[MAX_MSG];
  memcpy(payload, payload_theirs, length);
  payload[length] = 0;

  Debug.print("["); Debug.print(topic); Debug.print("] ");
  Debug.print((char *)payload);
  Debug.println();

  if (length < 6 + 2 * HASH_LENGTH + 1 + 12 + 1) {
    Log.println("Too short - ignoring.");
    return;
  };
  char * p = (char *)payload;

#define SEP(tok, err) char *  tok = strsepspace(&p); if (!tok) { Log.println(err); return; }

  SEP(sig, "No SIG header");

  if (strncmp(sig, "SIG/1", 5)) {
    Log.print("Unknown signature format <"); Log.print(sig); Log.println("> - ignoring.");
    return;
  }
  SEP(hmac, "No HMAC");
  SEP(beat, "No BEAT");

  char * rest = p;

  const char * hmac2 = hmacAsHex(passwd, beat, topic, rest);
  if (strcasecmp(hmac2, hmac)) {
    Log.println("Invalid signature - ignoring.");
    return;
  }
  unsigned long  b = strtoul(beat, NULL, 10);
  if (!b) {
    Log.print("Unparsable beat - ignoring.");
    return;
  };

  unsigned long delta = llabs((long long) b - (long long)beatCounter);

  // If we are still in the first hour of 1970 - accept any signed time;
  // otherwise - only accept things in a 4 minute window either side.
  //
  //  if (((!strcmp("beat", rest) || !strcmp("announce", rest)) &&  beatCounter < 3600) || (delta < 120)) {
  if ((beatCounter < 3600) || (delta < 120)) {
    beatCounter = b;
    if (delta > 10) {
      Log.print("Adjusting beat by "); Log.print(delta); Log.println(" seconds.");
    } else if (delta) {
      Debug.print("Adjusting beat by "); Debug.print(delta); Debug.println(" seconds.");
    }
  } else {
    Log.print("Good message -- but beats ignored as they are too far off ("); Log.print(delta); Log.println(" seconds).");
  };

  // handle a perfectly good message.
  if (!strncmp("announce", rest, 8)) {
    return;
  }

  if (!strncmp("beat", rest, 4)) {
    return;
  }

  if (!strncmp("ping", rest, 4)) {
    char buff[MAX_MSG];
    IPAddress myIp = WiFi.localIP();

    snprintf(buff, sizeof(buff), "ack %s %s %d.%d.%d.%d", master, nodename, myIp[0], myIp[1], myIp[2], myIp[3]);
    send(buff);
    return;
  }

  if (!strncmp("state", rest, 4)) {
    char buff[MAX_MSG];
    snprintf(buff, sizeof(buff), "state %d %s", machinestate, machinestateName[machinestate]);
    send(buff);
    return;
  }

#ifdef HASRFID
  if (handleRFID(b, rest))
    return;
#endif

  if (!strncmp("start", rest, 5)) {
    machinestate = POWERED;
    send("event start");
    return;
  }
  if (!strncmp("stop", rest, 4))  {
    machinestate = WAITINGFORCARD;
    send("event stop");
    return;
  }

  if (!strcmp("outoforder", rest)) {
    machinestate = OUTOFORDER;
    send("event outoforder");
    return;
  }

  Debug.printf("Do not know what to do with <%s>\n", rest);
  return;
}

void ACNode::loop() {
  static unsigned long last_wifi_ok = 0;
  if (WiFi.status() != WL_CONNECTED) {
    Debug.println("Lost WiFi connection.");
    if (machinestate <= WAITINGFORCARD)
      machinestate = NOCONN;
    if ( millis() - last_wifi_ok > 10000) {
      Log.printf("Connection to SSID:%s for 10 seconds now -- Rebooting...\n", WiFi.SSID().c_str());
      delay(500);
      ESP.restart();
    }
  } else {
    last_wifi_ok = millis();
  };

  static unsigned long last_mqtt_connect_try = 0;
  if (!client.connected()) {
    if (machinestate != NOCONN) {
      Debug.print("No MQTT connection: ");
      Debug.println(state2str(client.state()));
    };
    if (machinestate <= WAITINGFORCARD)
      machinestate = NOCONN;
    if (millis() - last_mqtt_connect_try > 3000 || last_mqtt_connect_try == 0) {
      reconnectMQTT();
      last_mqtt_connect_try = millis();
    }
  } else {
    if (machinestate == NOCONN)
      machinestate = WAITINGFORCARD;
  };
}

