#define OTA

#include <ESP8266WiFi.h>

#ifdef OTA
#include <WiFiUdp.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#endif

#include <PubSubClient.h>
#include <SPI.h>
#include <MFRC522.h>

#define BUILD  __FILE__ " " __DATE__ " " __TIME__

#include <sha256.h>

#if MQTT_MAX_PACKET_SIZE < 256
#error "You will need to increase te MQTT_MAX_PACKET_SIZE size a bit in PubSubClient.h"
#endif

#include "../../../../.passwd.h"

const char* ssid = WIFI_NETWORK;
const char* wifi_password = WIFI_PASSWD;
const char* mqtt_server = "space.makerspaceleiden.nl";

// MQTT topics are constructed from <prefix> / <dest> / <sender>
//
const char *mqtt_topic_prefix = "makerspace/ac";
const char *moi = "grindernode";    // Name of the sender
const char *machine = "grinder";
const char *master = "master";    // Destination for commands
const char *logpath = "log";       // Destination for human readable text/logging info.

// Password - specific for above 'moi' node name; and the name of the
// machine we control.
//
const char *passwd = ACNODE_PASSWD;

// Enduser visible Timeouts
//
const unsigned int   IDLE_TO        = (20 * 60 * 1000); // Auto disable/off after 20 minutes.
const unsigned int   CHECK_TO       = (3500); // Wait up to 3.5 second for result of card ok check.

// MQTT limits - which are partly ESP chip rather than protocol specific.
const unsigned int   MAX_TOPIC      = 64;
const unsigned int   MAX_MSG        = (MQTT_MAX_PACKET_SIZE - 32);
const unsigned int   MAX_TAG_LEN    = 10 ;/* Based on the MFRC522 header */
const         char* BEATFORMAT     = "%012u";
const unsigned int   MAX_BEAT       = 16;

// Wiring of current tablesaw/auto-dust control note.
//
const uint8_t PIN_RFID_SS    = 2;
const uint8_t PIN_RFID_RST   = 16;

const uint8_t PIN_LED        = 0; // red led to ground - led mounted inside start button.
const uint8_t PIN_POWER      = 15; // pulled low when not in use.

// GPIO4 - 10k pullup (not removed yet).
const uint8_t PIN_OPTO_OPERATOR      = 4; // front-switch 'off' -- capacitor charged by diode; needs to be pulled to ground to empty.
const uint8_t PIN_OPTO_ENERGIZED     = 5; // relay energized -- capacitor charged by diode; needs to be pulled to ground to empty.

// The RFID reader itself is connected to the
// hardwired MISO/MOSI and CLK pins (12, 13, 14)

// Comment out to reduce debugging output.
//
#define DEBUG  yes

// While we avoid using #defines, as per https://www.arduino.cc/en/Reference/Define, in above - in below
// case - the compiler was found to procude better code if done the old-fashioned way.
//
#ifdef DEBUG
#define Debug Serial
#else
#define Debug if (0) Log
#endif

typedef enum {
  SWERROR, OUTOFORDER, NOCONN, // some error - machine disabled.
  WRONGFRONTSWITCHSETTING,    // The switch on the front is in the 'on' setting; this blocks the operation of the on/off switch.
  DENIED,                     // we got a denied from the master -- flash an LED and then return to WAITINGFORCARD
  CHECKING,                   // we are are waiting for the master to respond -- flash an LED and then return to WAITINGFORCARD
  WAITINGFORCARD,             // waiting for card.
  POWERED,                    // Relay powered.
  ENERGIZED,                  // Got the OK; go to RUNNING once the green button at the back is pressed & operator switch is on.
  RUNNING,                    // Running - go to DUSTEXTRACT once the front switch is set to 'off' or to WAITINGFORCARD if red is pressed.
  NOTINUSE,
} machinestates_t;

const char *machinestateName[NOTINUSE] = {
  "Software Error", "Out of order", "No network",
  "Operator switch in wrong positson",
  "Tag Denied",
  "Checking tag",
  "Waiting for card to be presented",
  "Relay powered",
  "Energized",
  "Running",
};

static machinestates_t laststate;
unsigned long laststatechange = 0;
machinestates_t machinestate;

typedef enum { LED_OFF, LED_FAST, LED_ON, NEVERSET } ledstate;

ledstate lastred = NEVERSET;
ledstate red = NEVERSET;

void mqtt_callback(char* topic, byte* payload_theirs, unsigned int length);

WiFiClient espClient;
PubSubClient client(espClient);
MFRC522 mfrc522(PIN_RFID_SS, PIN_RFID_RST);

// Last tag swiped; as a string.
//
char lasttag[MAX_TAG_LEN * 4];      // 3 diigt byte and a dash or terminating \0. */
unsigned long lasttagbeat;          // Timestamp of last swipe.
unsigned long beatCounter = 0;      // My own timestamp - manually kept due to SPI timing issues.

// Quick 'tee' class - that sends all 'serial' port data also to the MQTT bus - to the 'log' topic
// if such is possible/enabled.
//
class Log : public Print {
  public:
    void begin(const char * prefix, int speed);
    virtual size_t write(uint8_t c);
  private:
    char logtopic[MAX_TOPIC], logbuff[MAX_MSG];
    size_t at;
};

void Log::begin(const char * prefix, int speed) {
  Serial.begin(speed);
  snprintf(logtopic, sizeof(logtopic), "%s/%s/%s", prefix, logpath, moi);
  logbuff[0] = 0; at = 0;
  return;
}

size_t Log::write(uint8_t c) {
  size_t r = Serial.write(c);

  if (c >= 32)
    logbuff[ at++ ] = c;

  if (c != '\n' && at <= sizeof(logbuff) - 1)
    return r;

  if (client.connected()) {
    logbuff[at++] = 0;
#ifdef DEBUG4
    Serial.print("debug: ");
    Serial.print(at);
    Serial.print(" ");
    Serial.println(logbuff);
#endif
    client.publish(logtopic, logbuff);
  };
  at = 0;

  return r;
}
Log Log;


// #if !digitalPinHasPWM(PIN_LED)
// #error "Cannot do PWN on the PIN_LED"
// #endif

void setRedLED(int state) {
  lastred = (ledstate) state;
  switch (lastred) {
    case LED_OFF:
      analogWrite(PIN_LED, 0);
      digitalWrite(PIN_LED, 0);
      break;
    case LED_FAST:
      analogWriteFreq(3);
      analogWrite(PIN_LED, PWMRANGE / 2);
      break;
    case LED_ON:
      analogWrite(PIN_LED, 0);
      digitalWrite(PIN_LED, 1);
      break;
  }
}


void setup() {
  pinMode(PIN_POWER, OUTPUT);
  digitalWrite(PIN_POWER, 0);

  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, 1);

  pinMode(PIN_OPTO_ENERGIZED, INPUT);
  pinMode(PIN_OPTO_OPERATOR, INPUT);

  setRedLED(LED_FAST);

  Log.begin(mqtt_topic_prefix, 115200);
  Log.println("\n\n\nBuild: " BUILD);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, wifi_password);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start < 5000)) {
    delay(100);
  };

  if (WiFi.status() != WL_CONNECTED) {
    Log.print("Connection to <"); Log.print(ssid); Log.println("> failed! Rebooting...");
    ESP.restart();
  }

  Log.print("Wifi connected to <"); Log.print(ssid); Log.println(">.");
#ifdef OTA
  ArduinoOTA.setPort(8266);
  ArduinoOTA.setHostname(moi);
  ArduinoOTA.setPassword((const char *)OTA_PASSWD);

  ArduinoOTA.onStart([]() {
    Log.println("OTA process started.");
  });
  ArduinoOTA.onEnd([]() {
    Log.println("OTA process completed. Resetting.");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Log.printf("%c%c%c%cProgress: %u%% ", 27, '[', '1', 'G', (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    setRedLED(LED_FAST);
    Log.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Log.println("OTA: Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Log.println("OTA: Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Log.println("OTA: Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Log.println("OTA: Receive Failed");
    else if (error == OTA_END_ERROR) Log.println("OTA: End Failed");
    else {
      Log.print("OTA: Error: ");
      Log.println(error);
    };
  });

  ArduinoOTA.begin();
#endif
  Log.print("IP address: ");
  Log.println(WiFi.localIP());

  SPI.begin();      // Init SPI bus
  mfrc522.PCD_Init();   // Init MFRC522
  mfrc522.PCD_DumpVersionToSerial();

  client.setServer(mqtt_server, 1883);
  client.setCallback(mqtt_callback);

  machinestate = WAITINGFORCARD;
  setRedLED(LED_ON);
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
  const char q2c[] = "0123456789abcdef";
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


void send(const char *payload) {
  char msg[ MAX_MSG];
  char beat[MAX_BEAT], topic[MAX_TOPIC];

  snprintf(beat, sizeof(beat), BEATFORMAT, beatCounter);
  snprintf(topic, sizeof(topic), "%s/%s/%s", mqtt_topic_prefix, master, moi);

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

void reconnectMQTT() {

  Debug.print("Attempting MQTT connection (State : ");
  Debug.print(state2str(client.state()));
  Debug.println(") ");

  //  client.connect(moi);
  //  client.subscribe(mqtt_topic_prefix);

  if (client.connect(moi)) {
    Log.println("(re)connected " BUILD);

    char topic[MAX_TOPIC];
    snprintf(topic, sizeof(topic), "%s/%s/%s", mqtt_topic_prefix, moi, master);
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

void mqtt_callback(char* topic, byte* payload_theirs, unsigned int length) {
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
  if (!strcmp("announce", rest)) {
    return;
  }

  if (!strcmp("beat", rest)) {
    return;
  }

  if (!strncmp("ping", rest, 4)) {
    char buff[MAX_MSG];
    IPAddress myIp = WiFi.localIP();

    snprintf(buff, sizeof(buff), "ack %s %s %d.%d.%d.%d", master, moi, myIp[0], myIp[1], myIp[2], myIp[3]);
    send(buff);
    return;
  }

  if (!strncmp("state", rest, 4)) {
    char buff[MAX_MSG];
    snprintf(buff, sizeof(buff), "state %d %s", machinestate, machinestateName[machinestate]);
    send(buff);
    return;
  }

  if (!strncmp("revealtag", rest, 9)) {
    if (b < lasttagbeat) {
      Log.println("Asked to reveal a tag I no longer have a record off, ignoring.");
      return;
    };
    char buff[MAX_MSG];
    snprintf(buff, sizeof(buff), "lastused %s", lasttag);
    send(buff);
    return;
  }

  if (!strncmp("approved", rest, 8) || !strncmp("energize", rest, 8)) {
    machinestate = POWERED;
    return;
  }

  if (!strncmp("denied", rest, 6) || !strncmp("unknown", rest, 7)) {
    Log.println("Flash LEDS");
    setRedLED(LED_FAST);
    delay(1000);
    setRedLED(LED_OFF);
    return;
  };

  if (!strcmp("outoforder", rest)) {
    machinestate = OUTOFORDER;
    send("event outoforder");
    return;
  }

  Debug.print("Do not know what to do with <"); Log.print(rest); Log.println("> - ignoring.");
  return;
}

unsigned int tock;
void handleMachineState() {
  tock++;

  int relayState = digitalRead(PIN_OPTO_ENERGIZED);
  int operatorSwitchState = digitalRead(PIN_OPTO_OPERATOR);

  int r = 0;
  if (relayState) r |= 1;
  if (operatorSwitchState) r |= 2;

  switch (r) {
    case 0: // On/off switch 'on' - blocking energizing.
      if (machinestate == RUNNING) {
        send("event stop-pressed");
      } else if (machinestate >= POWERED) {
        send("event powerdown");
        machinestate = WAITINGFORCARD;
      } else if (machinestate != WRONGFRONTSWITCHSETTING && machinestate > NOCONN) {
        send("event frontswitchfail");
        machinestate = WRONGFRONTSWITCHSETTING;
      }
      break;
    case 1: // On/off switch in the proper setting, ok to energize.
      if (machinestate > POWERED) {
        send("event stop-pressed");
        machinestate = WAITINGFORCARD;
      }
      if (machinestate == WRONGFRONTSWITCHSETTING) {
        send("event frontswitchokagain");
        machinestate = WAITINGFORCARD;
      };
      break;
    case 3: // Relay energized, but not running.
      if (machinestate == POWERED) {
        send("event start-pressed");
        machinestate = ENERGIZED;
      };
      if (machinestate == RUNNING) {
        send("event halted");
        machinestate = ENERGIZED;
      } else if (machinestate < ENERGIZED && machinestate > NOCONN) {
        static int last_spur_detect = 0;
        if (millis() - last_spur_detect > 500) {
          send("event spuriousbuttonpress?");
        };
        last_spur_detect = millis();
      };
      break;
    case 2: // Relay engergized and running.
      if (machinestate != RUNNING && machinestate > WAITINGFORCARD) {
        send("event running");
        machinestate = RUNNING;
      }
      break;
  };

  int relayenergized = 0;
  switch (machinestate) {
    case OUTOFORDER:
    case SWERROR:
    case NOCONN:
    case NOTINUSE:
      setRedLED(LED_FAST);
      break;
    case WRONGFRONTSWITCHSETTING:
      setRedLED(tock & 1 ? LED_OFF : LED_ON);
      break;
    case WAITINGFORCARD:
      setRedLED(tock & 127 ? LED_OFF : LED_ON);
      break;
    case CHECKING:
      setRedLED(LED_FAST);
      if (millis() - laststatechange > CHECK_TO) {
        setRedLED(LED_OFF);
        Log.print("D=");
        Log.print(millis() - laststatechange);
        Log.println("Returning to waiting for card - no response.");
        machinestate = WAITINGFORCARD;
      };
      break;
    case DENIED:
      setRedLED(LED_FAST);
      if (millis() - laststatechange > 500)
        machinestate = WAITINGFORCARD;
      break;
    case POWERED:
    case ENERGIZED:
      setRedLED(LED_ON);
      if (laststatechange < ENERGIZED) {
        send("event energized");
      };
      if (millis() - laststatechange > IDLE_TO) {
        send("event toolongidle");
        Log.println("Machine not used for more than 20 minutes; revoking access.");
        machinestate = WAITINGFORCARD;
      }
      relayenergized = 1;
      break;
    case RUNNING:
      setRedLED(LED_ON);
      relayenergized = 1;
      break;
  };

  digitalWrite(PIN_POWER, relayenergized);

  if (laststate != machinestate) {
#if DEBUG3
    Serial.print("State: <");
    Serial.print(machinestateName[laststate]);
    Serial.print("> to <");
    Serial.print(machinestateName[machinestate]);
    Serial.print(">");
    Serial.print(" 1="); Serial.print(v1);
    Serial.print(" 2="); Serial.print(v2);
    Serial.print(" red="); Serial.print(red);
    Serial.print(" P="); Serial.print(relayenergized);
    Serial.println();
#endif
    laststate = machinestate;
    laststatechange = millis();
  }
}

int checkTagReader() {
  if ( ! mfrc522.PICC_IsNewCardPresent())
    return 0;

  // Select one of the cards
  if ( ! mfrc522.PICC_ReadCardSerial())
    return 0;

  MFRC522::Uid uid = mfrc522.uid;
  if (uid.size == 0)
    return 0;

  lasttag[0] = 0;
  for (int i = 0; i < uid.size; i++) {
    char buff[5];
    snprintf(buff, sizeof(buff), "%s%d", i ? "-" : "", uid.uidByte[i]);
    strcat(lasttag, buff);
  }
  lasttagbeat = beatCounter;

  char beatAsString[ MAX_BEAT ];
  snprintf(beatAsString, sizeof(beatAsString), BEATFORMAT, beatCounter);
  Sha256.initHmac((const uint8_t*)passwd, strlen(passwd));
  Sha256.print(beatAsString);
  Sha256.write(uid.uidByte, uid.size);
  const char * tag_encoded = hmacToHex(Sha256.resultHmac());

  static char buff[MAX_MSG];
  snprintf(buff, sizeof(buff), "energize %s %s %s", moi, machine, tag_encoded);
  send(buff);

  return 1;
}
void loop() {

#ifdef DEBUG
  static int last_debug = 0, last_debug_state = -1;
  if (millis() - last_debug > 5000 || last_debug_state != machinestate) {
    Log.print("State: ");
    Log.print(machinestateName[machinestate]);

    int relayState = digitalRead(PIN_OPTO_ENERGIZED);
    Log.print(" relay="); Log.print(relayState);

    int operatorSwitchState = digitalRead(PIN_OPTO_OPERATOR);
    Log.print(" operator="); Log.print(operatorSwitchState);

    Log.print(" LED="); Log.print(lastred);
    Log.println(".");

    last_debug = millis();
    last_debug_state = machinestate;
  }
#endif

  handleMachineState();

  // Keepting time is a bit messy; the millis() wrap around and
  // the SPI access to the reader seems to mess with the millis().
  //
  static unsigned long last_loop = 0;
  if (millis() - last_loop >= 1000) {
    unsigned long secs = (millis() - last_loop + 500) / 1000;
    beatCounter += secs;
    last_loop = millis();
  }

  static unsigned long last_wifi_ok = 0;
  if (WiFi.status() != WL_CONNECTED) {
    Debug.println("Lost WiFi connection.");
    if (machinestate <= WAITINGFORCARD)
      machinestate = NOCONN;
    if ( millis() - last_wifi_ok > 10000) {
      Log.println("Connection dead for 10 seconds now -- Rebooting...");
      ESP.restart();
    }
  } else {
    last_wifi_ok = millis();
  };

#ifdef OTA
  ArduinoOTA.handle();
#endif

  client.loop();

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

#ifdef DEBUG3
  static unsigned long last_beat = 0;
  if (millis() - last_beat > 3000 && client.connected()) {
    send("ping");
    last_beat = millis();
  }
#endif

  if (machinestate >= WAITINGFORCARD && millis()-laststatechange > 1500) {
    if (checkTagReader()) {
      laststatechange = millis();
      if (machinestate >= ENERGIZED)
        machinestate = WAITINGFORCARD;
      else
        machinestate = CHECKING;
    }
  }
}
