
#ifdef ARDUINO_ESP8266_ESP12
// We guess that ESP12 devices have enough memory for OTA. Unfortunately there is
// no #define of the memory specified in the tools menu/hardware/boarts.txt file.
//
#define OTA
#endif

// While the ESP01s generally have a lot less memory. Our code needs to stay
// below 236KB ( (512/2-4KB-16KB) in order to allow OTA on a 512kB flash.
//
#ifdef ARDUINO_ESP8266_ESP01
#endif

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

#include "../../../.passwd.h"

const char* ssid = WIFI_NETWORK;
const char* wifi_password = WIFI_PASSWD;
const char* mqtt_server = "space.makerspaceleiden.nl";

// MQTT topics are constructed from <prefix> / <dest> / <sender>
//
const char *mqtt_topic_prefix = "test";
const char *moi = "woodbandsawnode";    // Name of the sender
const char *machine = "woodbandsaw";
const char *master = "master";    // Destination for commands
const char *logpath = "log";       // Destination for human readable text/logging info.

// Password - specific for above 'moi' node name; and the name of the
// machine we control.
//
const char *passwd = ACNODE_PASSWD;

// Enduser visible Timeouts
//
const unsigned int   DUSTEXTRACT_TO      = (5 * 1000); // 5 seconds extra DUSTEXTRACT on
const unsigned int   IDLE_TO        = (20 * 60 * 1000); // Auto disable/off after 20 minutes.
const unsigned int   CHECK_TO       = (3 * 1000); // Wait up to 3 second for result of card ok check.

// MQTT limits - which are partly ESP chip rather than protocol specific.
const unsigned int   MAX_TOPIC      = 64;
const unsigned int   MAX_MSG        = (MQTT_MAX_PACKET_SIZE - 32);
const unsigned int   MAX_TAG_LEN    = 10 ;/* Based on the MFRC522 header */
const         char* BEATFORMAT     = "%012u";
const unsigned int   MAX_BEAT       = 16;

// Wiring of current tablesaw/auto-dust control note.
//
const uint8_t PIN_RFID_SS    = 2;
const uint8_t PIN_LEDS       = 0; // red led to ground, green led to rail (with extra resistor to ensure boot from flash)
const uint8_t PIN_OVERLOAD   = 0; // switch on thermal sensor to ground

const uint8_t PIN_POWER      = 15; // pulled low when not in use.
const uint8_t PIN_DUSTEXTRACT     = 16; // pulled low when not in use.

const uint8_t PIN_OPTO1      = 4; // front-switch 'off' -- capacitor charged by diode; needs to be pulled to ground to empty.
const uint8_t PIN_OPTO2      = 5; // relay energized -- capacitor charged by diode; needs to be pulled to ground to empty.

// Comment out to reduce debugging output.
//
// #define DEBUG  yes

// While we avoid using #defines, as per https://www.arduino.cc/en/Reference/Define, in above - in below
// case - the compiler was found to procude better code if done the old-fashioned way.
//
#ifdef DEBUG
#define Debug Log
#else
#define Debug if (0) Log
#endif

typedef enum {
  SWERROR, OUTOFORDER, NOCONN, // some error - machine disabled.
  OVERLOAD,                   // current overload protection has tripped. Machine disabled.
  WRONGFRONTSWITCHSETTING,    // The switch on the front is in the 'on' setting; this blocks the operation of the on/off switch.
  DENIED,                     // we got a denied from the master -- flash an LED and then return to WAITINGFORCARD
  CHECKING,                   // we are are waiting for the master to respond -- flash an LED and then return to WAITINGFORCARD
  WAITINGFORCARD,             // waiting for card.
  ENERGIZED,                  // Got the OK; go to RUNNING once the green button at the back is pressed & operator switch is on.
  RUNNING,                    // Running - go to DUSTEXTRACT once the front switch is set to 'off' or to WAITINGFORCARD if red is pressed.
  DUSTEXTRACT                     // Letting dust control do its thing; then drop back to ENERGIZED.
} machinestates_t;

char * machinestateName[] = {
  "Software Error", "Out of order", "No network",
  "Current overload tripped",
  "Operator switch in wrong positson",
  "Tag Denied",
  "Checking tag",
  "Waiting for card to be presented",
  "Energized",
  "Running",
  "Postrun Dust extraction"
};

static machinestates_t laststate;
unsigned long laststatechange = 0;
machinestates_t machinestate;

typedef enum { LED_OFF, LED_SLOW, LED_FAST, LED_ON } ledstate;
ledstate red, green;

const unsigned int DFAST = 100;      // 10 times/second - swap/flash
const unsigned int DSLOW  = 5 * DFAST; // twice a second.
const unsigned int DMAX  = 1000;     // we in effect for updates every second - even when in steady state.

WiFiClient espClient;
PubSubClient client(espClient);
MFRC522 mfrc522(PIN_RFID_SS, PIN_LEDS);  // Reset not wired up - it is only called during boot - so the red LED will just flash shortly.

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
    int at;
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

ledstate lastred, lastgreen;
void restoreLeds() {
  int r, g;
  static int i;
  switch (lastred) {
    case LED_OFF: r = 0; break;
    case LED_SLOW: r =  (millis() / DSLOW) & 1; break;
    case LED_FAST: r = (millis() / DFAST) & 1; break;
    case LED_ON: r = (green == LED_ON) ? (i++ & 1) : 1;
  }
  switch (lastgreen) {
    case LED_OFF: g = 0; break;
    case LED_SLOW: g =  (1 + millis() / DSLOW) & 1; break;
    case LED_FAST: g = (1 + millis() / DFAST) & 1; break;
    case LED_ON: g = (red == LED_ON) ? (i++ & 1) : 1;
  }
  if (r == g) {
    pinMode(PIN_LEDS, INPUT);
  }
  else if (r) {
    pinMode(PIN_LEDS, OUTPUT);
    digitalWrite(PIN_LEDS, 0);
  } else {
    pinMode(PIN_LEDS, OUTPUT);
    digitalWrite(PIN_LEDS, 1);
  };
}

void tock() {
  static unsigned long lasttock = 0;
  unsigned d  = DMAX;

  if (red == LED_SLOW || green == LED_SLOW)
    d = DSLOW;

  if (red == LED_FAST || green == LED_FAST)
    d = DFAST;

  if (millis() - lasttock < d && lastgreen == green && lastred == red)
    return;

  lastred = red; lastgreen = green; lasttock = millis();
  restoreLeds();
}


void tock1second() {
  unsigned long now = millis();
  while (millis() - now < 1000)
    tock();
}

void setup() {
  pinMode(PIN_DUSTEXTRACT, OUTPUT); digitalWrite(PIN_DUSTEXTRACT, 0);
  pinMode(PIN_POWER, OUTPUT); digitalWrite(PIN_POWER, 0);

  red = green = LED_FAST;
  tock();

  Log.begin(mqtt_topic_prefix, 115200);
  Log.println("\n\n\nBuild: " BUILD);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, wifi_password);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start < 5000)) {
    tock();
    delay(100);
  };

  if (WiFi.status() != WL_CONNECTED) {
    Log.print("Connection to <"); Log.print(ssid); Log.println("> failed! Rebooting...");
    green = LED_OFF;
    for (int i = 0; i < 50; i++) {
      delay(100);
      tock();
    }
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
  red = LED_ON;
  green = LED_OFF;
  tock();
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
      return "Unknown MQTT error";
  }
#else
  static char buff[5]; snprintf(buff, sizeof(buff), "%d", state);
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
  static char hex[2 * HASH_LENGTH + 1];

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

  uint8_t* hash;
  snprintf(beat, sizeof(beat), BEATFORMAT, beatCounter);
  snprintf(topic, sizeof(topic), "%s/%s/%s", mqtt_topic_prefix, master, moi);

  msg[0] = 0;
  strcat(msg, "SIG/1.0 ");
  strcat(msg, hmacAsHex(passwd, beat, topic, payload));
  strcat(msg, " ");
  strcat(msg, beat);
  strcat(msg, " ");
  strcat(msg, payload);
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
    snprintf(buff, sizeof(buff), "announce %s", WiFi.localIP());
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
  if (((!strcmp("beat", rest) || !strcmp("announce", rest)) &&  beatCounter < 3600) || (delta < 120)) {
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
    snprintf(buff, sizeof(buff), "ack %s %s %s", master, moi, WiFi.localIP());
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
    machinestate = ENERGIZED;
    return;
  }

  if (!strncmp("denied", rest, 6) || !strncmp("unknown", rest, 7)) {
    Log.println("Flash LEDS");
    green = LED_OFF; red = LED_FAST;
    tock1second();
    green = LED_ON; red = LED_OFF;
    tock();
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

void handleMachineState() {
#if 0
  static unsigned long lastdischarge = 0;
  unsigned int v1 = 0, v2 = 0;
  if (lastdischarge) {
    pinMode(PIN_OPTO1, INPUT); pinMode(PIN_OPTO2, INPUT);
    while (millis() - lastdischarge < 50) {
      v1 += digitalRead(PIN_OPTO1);
      v2 += digitalRead(PIN_OPTO2);
      delay(1);
    }
  }
  // Discharge the capacitors once we are done.
  //
  pinMode(PIN_OPTO1, OUTPUT); pinMode(PIN_OPTO2, OUTPUT);
  digitalWrite(PIN_OPTO1, 0); digitalWrite(PIN_OPTO2, 0);
  delay(1);
  pinMode(PIN_OPTO1, INPUT); pinMode(PIN_OPTO2, INPUT);
  lastdischarge = millis();
#endif

  unsigned int v1 = 0, v2 = 0;
  pinMode(PIN_OPTO1, OUTPUT); pinMode(PIN_OPTO2, OUTPUT);
  digitalWrite(PIN_OPTO1, 0);  digitalWrite(PIN_OPTO2, 0);
  delay(1);
  pinMode(PIN_OPTO1, INPUT); pinMode(PIN_OPTO2, INPUT);
  //  for (int j = 0; j < 5 || (v1+v2==0 && j < 30); j++) {
  for (int j = 0; j < 50; j++) {
    v1 += digitalRead(PIN_OPTO1);
    v2 += digitalRead(PIN_OPTO2);
    delay(2);
  };

  // The overload pin is shared with the LED; so do the
  // measurement quickly - and then restore the LEDs.
  pinMode(PIN_OVERLOAD, INPUT);
  delay(1);
  int overload = digitalRead(PIN_OVERLOAD) ? 0 : 1;
  restoreLeds();

  int r = 0;
  if (v1) r |= 1;
  if (v2) r |= 2;
  switch (r) {
    case 0: // On/off switch 'on' - blocking energizing.
      if (machinestate >= ENERGIZED) {
        send("event powerdown");
        machinestate = WAITINGFORCARD;
      } else if (machinestate != WRONGFRONTSWITCHSETTING && machinestate > OVERLOAD) {
        send("event frontswitchfail");
        machinestate = WRONGFRONTSWITCHSETTING;
      }
      break;
    case 1: // On/off switch in the proper setting, ok to energize.
      if (machinestate == WRONGFRONTSWITCHSETTING) {
        send("event frontswitchokagain");
        machinestate = WAITINGFORCARD;
      };
      break;
    case 3: // Relay energized, but not running.
      if (machinestate == RUNNING) {
        send("event halted");
        machinestate = DUSTEXTRACT;
      } else if (machinestate < ENERGIZED && machinestate > OVERLOAD) {
        send("event spuriousbuttonpress?");
      };
      break;
    case 2: // Relay engergized and running.
      if (machinestate != RUNNING && machinestate > WAITINGFORCARD) {
        send("event running");
        machinestate = RUNNING;
      }
      break;
  };

  // Overload wins from all of the above. Hence no 'return' prior to this point.
  //
  if (overload) {
    if (machinestate != OVERLOAD) {
      send("event overload");
    };
    machinestate = OVERLOAD;
  } else {
    if (machinestate == OVERLOAD) {
      machinestate = WAITINGFORCARD;
      send("event no-overload");
    };
  };

  int relayenergized = 0;
  int dustextract = 0;
  switch (machinestate) {
    case OUTOFORDER:
      red = LED_FAST; green = LED_FAST;
      break;
    case OVERLOAD:
      red = LED_FAST; green = LED_OFF;
      break;
    case NOCONN:
      red = LED_FAST; green = LED_FAST;
      break;
    case WRONGFRONTSWITCHSETTING:
      red = LED_SLOW; green = LED_ON;
      break;
    case WAITINGFORCARD:
      red = LED_OFF; green = LED_ON;
      break;
    case CHECKING:
      red = LED_OFF; green = LED_FAST;
      if (millis() - laststatechange > CHECK_TO)
        machinestate = WAITINGFORCARD;
      break;
    case DENIED:
      red = LED_FAST; green = LED_OFF;
      if (millis() - laststatechange > 500)
        machinestate = WAITINGFORCARD;
      break;
    case ENERGIZED:
      red = LED_ON; green = LED_OFF;
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
    case DUSTEXTRACT:
      if (millis() - laststatechange > DUSTEXTRACT_TO) {
        send("event dustextractoff");
        machinestate = ENERGIZED;
      };
      relayenergized = 1; dustextract = 1;
      break;
    case RUNNING:
      red = LED_ON; green = LED_OFF;
      relayenergized = 1; dustextract = 1;
      break;
  };

  digitalWrite(PIN_POWER, relayenergized);
  digitalWrite(PIN_DUSTEXTRACT, dustextract);

  if (laststate != machinestate) {
#if DEBUG3
    Serial.print("State: <");
    Serial.print(machinestateName[laststate]);
    Serial.print("> to <");
    Serial.print(machinestateName[machinestate]);
    Serial.print("> O="); Serial.print(overload);
    Serial.print(" 1="); Serial.print(v1);
    Serial.print(" 2="); Serial.print(v2);
    Serial.print(" red="); Serial.print(red);
    Serial.print(" green="); Serial.print(green);
    Serial.print(" P="); Serial.print(relayenergized);
    Serial.print(" D="); Serial.print(dustextract);
    Serial.println();
#endif
    laststate = machinestate;
    laststatechange = millis();
  }
}

int checkTagReader() {

#if FAKEREADERTEST
  MFRC522::Uid uid = { 0, 0, 0 };

  static unsigned long test = 0;
  if (millis() - test > 15000) {
    uid = { 4, { 1, 2, 3, 4}, 0 };
    test = millis();
  }
#else
  // Look for new cards
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

  char buff[250];
  snprintf(buff, sizeof(buff), "energize % s % s % s", moi, machine, tag_encoded);
  send(buff);

  return 1;
}
void loop() {
#ifdef DEBUG
  static int last_debug = 0;
  if (millis()-last_debug > 1500) {
    Log.print("State: ");
    Log.println(machinestateName[machinestate]);
  }
#endif
  
  handleMachineState();
  tock();

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
    if (machinestate == NOCONN)
      machinestate == WAITINGFORCARD;
    last_wifi_ok = millis();
  };

#ifdef OTA
  ArduinoOTA.handle();
#endif

  client.loop();

  static unsigned long last_mqtt_connect_try = 0;
  if (!client.connected()) {
    Debug.println("Lost MQTT connection.");
    if (machinestate <= WAITINGFORCARD)
      machinestate = NOCONN;
    if (millis() - last_mqtt_connect_try > 5000 || last_mqtt_connect_try == 0) {
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

  if (machinestate == WAITINGFORCARD && millis() - laststatechange > 500)
    if (checkTagReader())
      machinestate = CHECKING;
}

