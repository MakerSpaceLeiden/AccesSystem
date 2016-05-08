
// Node MCU has a weird mapping...
//
#define LED_GREEN   16 // D0 -- LED inside the on/off toggle switch
#define LED_ORANGE  5  // D1 -- LED inside the orange, bottom, push button.
#define RELAY       4  // D2 -- relay (220V, 10A on just the L)
#define PUSHBUTTON  0  // D3 -- orange push button; 0=pressed, 1=released

// With the new OTA code -- All ESPs have
// enough room for the code -- though still need
// Over 328kB free to actually use it.
//
#define OTA

// Allow the unit to go into AP mode for reconfiguration
// if no wifi network is found.
//
//
#define CONFIGAP

// Comment out to reduce debugging output. Note that most key
// debugging is only visible on the serial port.
//
// #define DEBUG

#include <ESP8266WiFi.h>
#include <Ticker.h>

#ifdef OTA
#include <WiFiUdp.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#endif

#ifdef CONFIGAP
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#endif

#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson (change isnan()/isinf() to __builtin_isnXXX() if needed).
#include <PubSubClient.h>        // https://github.com/knolleary/

#ifdef HASRFID
#include <MFRC522.h>
#endif

#include <SPI.h>
#include <FS.h>

#define BUILD  __FILE__ " " __DATE__ " " __TIME__

#include <sha256.h>

#if MQTT_MAX_PACKET_SIZE < 256
#error "You will need to increase te MQTT_MAX_PACKET_SIZE size a bit in PubSubClient.h"
#endif

#include "../../../../.passwd.h"

// MQTT limits - which are partly ESP chip rather than protocol specific.
const unsigned int   MAX_NAME        = 24;
const unsigned int   MAX_TOPIC      = 64;
const unsigned int   MAX_MSG        = (MQTT_MAX_PACKET_SIZE - 32);
const unsigned int   MAX_TAG_LEN    = 10 ;/* Based on the MFRC522 header */
const         char* BEATFORMAT     = "%012u";
const unsigned int   MAX_BEAT       = 16;

#ifndef MQTT_DEFAULT_PORT
#define MQTT_DEFAULT_PORT (1883)
#endif

// const char ssid[34] = WIFI_NETWORK ;
// const char wifi_password[34] = WIFI_PASSWD;
char mqtt_server[34] = "not-yet-cnf-mqtt";
uint16_t mqtt_port = MQTT_DEFAULT_PORT;

// MQTT topics are constructed from <prefix> / <dest> / <sender>
//
char mqtt_topic_prefix[MAX_TOPIC] = "test";
char moi[MAX_NAME] = "exhaustnode";    // Name of the sender
char machine[MAX_NAME] = "fan";
char master[MAX_NAME] = "master";    // Destination for commands
char logpath[MAX_NAME] = "log";       // Destination for human readable text/logging info.

// Password - specific for above 'moi' node name; and the name of the
// machine we control.
//
char passwd[MAX_NAME] = ACNODE_PASSWD;

#ifdef DEBUG
#define Debug Serial
#else
#define Debug if (0) Log
#endif

typedef enum {
  SWERROR, OUTOFORDER, NOCONN, // some error - machine disabLED.
#if 0
  WRONGFRONTSWITCHSETTING,    // The switch on the front is in the 'on' setting; this blocks the operation of the on/off switch.
  DENIED,                     // we got a denied from the master -- flash an LED and then return to WAITINGFORCARD
  CHECKING,                   // we are are waiting for the master to respond -- flash an LED and then return to WAITINGFORCARD
#endif
  WAITINGFORCARD,             // waiting for card.
  POWERED,                    // Relay powered.
#if 0
  ENERGIZED,                  // Got the OK; go to RUNNING once the green button at the back is pressed & operator switch is on.
  RUNNING,                    // Running - go to DUSTEXTRACT once the front switch is set to 'off' or to WAITINGFORCARD if red is pressed.
#endif
  DUSTEXTRACT,
  NOTINUSE,
} machinestates_t;

const char *machinestateName[NOTINUSE] = {
  "Software Error", "Out of order", "No network",
  "Fan off",
  "Fan on",
  "Fan runout",
};

static machinestates_t laststate;
unsigned long laststatechange = 0;
machinestates_t machinestate;

Ticker greenLEDTicker;
Ticker orangeLEDTicker;

typedef enum { LED_OFF, LED_FLASH, LED_SLOW, LED_FAST, LED_ON, NEVERSET } LEDstate;
const char *ledstateName[ NEVERSET ] = { "off", "slow", "fast", "on" };
LEDstate lastorange = NEVERSET;
LEDstate lastgreen = NEVERSET;

void mqtt_callback(char* topic, byte* payload_theirs, unsigned int length);

WiFiClient espClient;
PubSubClient client(espClient);
#ifdef HASRFID
MFRC522 mfrc522(PIN_RFID_SS, PIN_RFID_RST);
#endif

// Last tag swiped; as a string.
//
char lasttag[MAX_TAG_LEN * 4];      // 3 diigt byte and a dash or terminating \0. */
unsigned long lasttagbeat;          // Timestamp of last swipe.
unsigned long beatCounter = 0;      // My own timestamp - manually kept due to SPI timing issues.

// Quick 'tee' class - that sends all 'serial' port data also to the MQTT bus - to the 'log' topic
// if such is possible/enabLED.
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
  while (!Serial) {
    delay(100);
  }
  Serial.print("\n\n\n\n\n");
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

void flipPin(uint8_t pin) {
  static unsigned int tock = 0;
  if (pin & 128) {
    digitalWrite(pin & 127, !(tock & 31));
  } else {
    digitalWrite(pin, !digitalRead(pin));
  }
  tock++;
}

void setLED(Ticker & t, uint8_t pin, int state) {
  switch ((LEDstate) state) {
    case LED_OFF:
      t.detach();
      digitalWrite(pin, 0);
      break;
    case LED_FLASH:
     pin |= 128;
      t.attach_ms(100, flipPin,  pin); // no need to detach - code will disarm and re-use existing timer.
      break;
    case LED_SLOW:
      digitalWrite(pin, 1);
      t.attach_ms(500, flipPin, pin); // no need to detach - code will disarm and re-use existing timer.
      break;
    case LED_FAST:
      t.attach_ms(100, flipPin, pin); // no need to detach - code will disarm and re-use existing timer.
      break;
    case LED_ON:
      t.detach();
      digitalWrite(pin, 1);
      break;
  }
}

void setGreenLED(int state) {
  if (lastgreen != state)
    setLED(greenLEDTicker, LED_GREEN, state);
  lastgreen = (LEDstate) state;
}
void setOrangeLED(int state) {
  if (lastorange != state)
    setLED(orangeLEDTicker, LED_ORANGE, state);
  lastorange = (LEDstate) state;
}

#ifdef CONFIGAP
//flag for saving data
bool shouldSaveConfig = false;

//callback notifying us of the need to save config
void saveConfigCallback () {
  shouldSaveConfig = true;
}
#endif

void setup() {
  digitalWrite(LED_GREEN, 1);
  digitalWrite(RELAY, 0);

  pinMode(BUILTIN_LED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_ORANGE, OUTPUT);
  pinMode(RELAY, OUTPUT);
  pinMode(PUSHBUTTON, INPUT);

  setGreenLED(LED_FAST);
  setOrangeLED(LED_OFF);

  Log.begin(mqtt_topic_prefix, 115200);
  Log.println("\n\n\nBuild: " BUILD);

#ifdef DEBUG
  uint32_t realSize = ESP.getFlashChipRealSize();
  uint32_t ideSize = ESP.getFlashChipSize();
  FlashMode_t ideMode = ESP.getFlashChipMode();

  Serial.printf("Flash real id:   %08X\n", ESP.getFlashChipId());
  Serial.printf("Flash real size: %u bytes (%u MB)\n\n", realSize, realSize >> 20);

  Serial.printf("Flash ide  size: %u bytes\n", ideSize);
  Serial.printf("Flash ide speed: %u\n", ESP.getFlashChipSpeed());
  Serial.printf("Flash ide mode:  %s\n", (ideMode == FM_QIO ? "QIO" : ideMode == FM_QOUT ? "QOUT" : ideMode == FM_DIO ? "DIO" : ideMode == FM_DOUT ? "DOUT" : "UNKNOWN"));

  if (ideSize != realSize) {
    Serial.println("Flash Chip configuration wrong!\n");
  } else {
    Serial.println("Flash Chip configuration ok.\n");
  }
#endif

#ifdef CONFIGAP
  if (SPIFFS.begin()) {
    Debug.println("SPIFFS opened ok");
  } else {
    Log.println("Dead SPIFFS - formatting it..");
    SPIFFS.format();
  }

  static int debounce = 0;
  while (digitalRead(PUSHBUTTON) == 0 && debounce < 5) {
    debounce++;
    delay(5);
  };
  if (debounce >= 5)  {
    Log.print("Going into AP mode config mode\n");
    setGreenLED(LED_OFF);
    setOrangeLED(LED_FAST);

    WiFiManager wifiManager;
    wifiManager.setDebugOutput(0); // avoid sensitive stuff to appear needlessly.

    char mqtt_port_buff[5];
    char passwd_buff[MAX_NAME];
    snprintf(mqtt_port_buff, sizeof(mqtt_port_buff), "%d", mqtt_port);
    passwd_buff[0] = 0; // force user to (re)set the password; rather than reveal anything.

    WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, sizeof(mqtt_server));
    WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port_buff, sizeof(mqtt_port_buff));
    WiFiManagerParameter custom_node("node", "node name", moi, sizeof(moi));
    WiFiManagerParameter custom_machine("machine", "machine", machine, sizeof(machine));
    WiFiManagerParameter custom_prefix("topic_prefix", "topix prefix", mqtt_topic_prefix, sizeof(mqtt_topic_prefix));
    WiFiManagerParameter custom_passwd("passwd", "shared secret", passwd_buff, sizeof(passwd_buff));
    WiFiManagerParameter custom_master("master", "master node", master, sizeof(master));
    WiFiManagerParameter custom_logpath("logpath", "logpath", logpath, sizeof(logpath));

    wifiManager.addParameter(&custom_mqtt_server);
    wifiManager.addParameter(&custom_mqtt_port);
    wifiManager.addParameter(&custom_logpath);
    wifiManager.addParameter(&custom_prefix);

    wifiManager.addParameter(&custom_node);
    wifiManager.addParameter(&custom_machine);

    wifiManager.addParameter(&custom_master);
    wifiManager.addParameter(&custom_passwd);

    wifiManager.setSaveConfigCallback(saveConfigCallback);

    // wifiManager.autoConnect();
    String ssid = "ACNode CNF " + WiFi.macAddress();
    if (!wifiManager.startConfigPortal(ssid.c_str()))
    {
      Serial.println("failed to connect and hit timeout - rebooting");
      delay(1000);
      //reset and try again, or maybe put it to deep sleep
      ESP.reset();
      delay(5000);
    }

    if (shouldSaveConfig) {
      Serial.println("We got stuff to save!");

      DynamicJsonBuffer jsonBuffer;
      JsonObject& json = jsonBuffer.createObject();

      json["mqtt_server"] = custom_mqtt_server.getValue();
      json["mqtt_port"] = custom_mqtt_port.getValue();
      json["moi"] = custom_node.getValue();
      json["machine"] = custom_machine.getValue();
      json["master"] = custom_master.getValue();
      json["prefix"] = custom_prefix.getValue();
      json["passwd"] = custom_passwd.getValue();
      json["logpath"] = custom_logpath.getValue();

      File configFile = SPIFFS.open("/config.json", "w");
      if (!configFile) {
        Serial.println("failed to open config file for writing");
      }

      // This will contain things like passwords in the clear
      // json.prettyPrintTo(Serial);

      json.printTo(configFile);
      configFile.close();
    }
  }

  File configFile = SPIFFS.open("/config.json", "r");
  if (configFile) {
    Serial.println("opening config file");
    size_t size = configFile.size();
    std::unique_ptr<char[]> buf(new char[size]);

    configFile.readBytes(buf.get(), size);
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.parseObject(buf.get());
    if (json.success()) {
      char tmp_port[32];

#define JSONR(d,v) { \
    const char * str = json[v]; \
    if (str) strncpy(d,str,sizeof(d)); \
    Debug.printf("%s=\"%s\" ==> %s\n", v, str ? (strcmp(v,"passwd") ? str : "****") : "\\0",  (strcmp(v,"passwd") ? d : "****"));\
  }
      JSONR(mqtt_server, "mqtt_server");
      JSONR(tmp_port, "mqtt_port");
      JSONR(moi, "moi");
      JSONR(mqtt_topic_prefix, "prefix");
      JSONR(passwd, "passwd");
      JSONR(logpath, "logpath");
      JSONR(master, "master");
      JSONR(machine, "machine");

      int p = atoi(tmp_port);
      if (p == 0) p = MQTT_DEFAULT_PORT;
      if (p < 65564) mqtt_port = p;
    } else { // if valid json
      Log.println("JSON invalid - ignored.");
    };
  } else { // if configfile
    Log.println("No JSON config file - odd");
  };
#endif

  WiFi.mode(WIFI_STA);
  WiFi.SSID();
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start < 5000)) {
    delay(1000);
  };

  if (WiFi.status() != WL_CONNECTED) {
    Log.printf("Connection to <%s>\n", WiFi.SSID().c_str());
    setOrangeLED(LED_FAST);
    Log.println("Rebooting...");
    delay(5000);
    ESP.restart();
  }

  Log.printf("Wifi connected to <%s>\n", WiFi.SSID().c_str());
#ifdef OTA
  ArduinoOTA.setPort(8266);
  ArduinoOTA.setHostname(moi);
  ArduinoOTA.setPassword((const char *)OTA_PASSWD);

  ArduinoOTA.onStart([]() {
    Log.println("OTA process started.");
    setGreenLED(LED_SLOW);
    setOrangeLED(LED_SLOW);
  });
  ArduinoOTA.onEnd([]() {
    Log.println("OTA process completed. Resetting.");
    setGreenLED(LED_OFF);
    setOrangeLED(LED_ON);
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Log.printf("%c%c%c%cProgress: %u%% ", 27, '[', '1', 'G', (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    setGreenLED(LED_FAST);
    setOrangeLED(LED_FAST);
    Log.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Log.println("OTA: Auth failed");
    else if (error == OTA_BEGIN_ERROR) Log.println("OTA: Begin failed");
    else if (error == OTA_CONNECT_ERROR) Log.println("OTA: Connect failed");
    else if (error == OTA_RECEIVE_ERROR) Log.println("OTA: Receive failed");
    else if (error == OTA_END_ERROR) Log.println("OTA: End failed");
    else {
      Log.print("OTA: Error: ");
      Log.println(error);
    };
  });
  ArduinoOTA.begin();
  Log.print("IP address (and OTA enabled): ");
#else
  Log.print("IP address: ");
#endif
  Log.println(WiFi.localIP());

  SPI.begin();      // Init SPI bus
#ifdef HASRFID
  mfrc522.PCD_Init();   // Init MFRC522
  mfrc522.PCD_DumpVersionToSerial();
#endif

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqtt_callback);

  machinestate = WAITINGFORCARD;
  setGreenLED(LED_ON);

#ifdef DEBUG
  {
    Dir dir = SPIFFS.openDir("/");
    Debug.println("SPI File System:");
    while (dir.next()) {
      String fileName = dir.fileName();
      size_t fileSize = dir.fileSize();
      Debug.printf("FS File: %s, size: %d\n", fileName.c_str(), fileSize);
    }
    Debug.printf("\n");
  }
#endif
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

  Debug.printf("Attempting MQTT connection to %s:%d (State : %s\n",
               mqtt_server, mqtt_port, state2str(client.state()));

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

void mqtt_callback(char* topic, byte * payload_theirs, unsigned int length) {
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

#ifdef HASRFID
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
  if (!strncmp("denied", rest, 6) || !strncmp("unknown", rest, 7)) {
    Log.println("Flash LEDS");
    setOrangeLED(LED_FAST);
    delay(1000);
    setRedLED(LED_OFF);
    return;
  };

  if (!strncmp("approved", rest, 8) || !strncmp("energize", rest, 8)) {
    machinestate = POWERED;
    return;
  }
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

unsigned int tock;
void handleMachineState() {
  tock++;

  // If we hold the button long-ish; it will start flip-flopping
  // with a 3 second cycle; but short jab's will immediately
  // allow it to go on/off.
  //
  static unsigned long last_button_detect = 0;
  if (digitalRead(PUSHBUTTON) == 0 && millis() - last_button_detect > 1500) {
    last_button_detect = millis();

    if (machinestate == WAITINGFORCARD) {
      machinestate = POWERED;
      send("event manual-start");
    }
    else if (machinestate >= POWERED) {
      machinestate = WAITINGFORCARD;
      send("event manual-stop");
    } else 
      send("event spurious-button-press");
  }

  if (digitalRead(PUSHBUTTON) == 1 && millis() - last_button_detect > 400) {
    last_button_detect =  0;
  }
  
  if (laststate <= NOCONN && machinestate > NOCONN)
    setGreenLED(LED_ON);

  int relayenergized = 0;
  switch (machinestate) {
    case OUTOFORDER:
    case SWERROR:
    case NOCONN:
    case NOTINUSE:
      setGreenLED(LED_FAST);
      break;
#if 0
    case WRONGFRONTSWITCHSETTING:
      setRedLED(tock & 1 ? LED_OFF : LED_ON);
      break;
#endif
    case WAITINGFORCARD:
      if (machinestate <= NOCONN)
        setGreenLED(LED_ON);
      setOrangeLED(LED_FLASH);
      break;
#if 0
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
#endif
    case POWERED:
#if 0
      if (laststatechange < ENERGIZED) {
        send("event energized");
      };
    // intentional fall through
    case ENERGIZED:
#endif
      setOrangeLED(LED_ON);
#if 0
      if (millis() - laststatechange > IDLE_TO) {
        send("event toolongidle");
        Log.println("Machine not used for more than 20 minutes; revoking access.");
        machinestate = WAITINGFORCARD;
      }
#endif

      relayenergized = 1;
      break;
#if 0
    case RUNNING:
      setRedLED(LED_ON);
      relayenergized = 1;
      break;
#endif
  };

  digitalWrite(RELAY, relayenergized);

  if (laststate != machinestate) {
#ifdef DEBUG3
    Serial.printf("State: <%s> to <%s>",
                  machinestateName[laststate],
                  machinestateName[machinestate]);

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

#ifdef HASRFID
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
#endif

void loop() {

#ifdef DEBUG
  static int last_debug = 0, last_debug_state = -1;
  if (millis() - last_debug > 5000 || last_debug_state != machinestate) {
    Log.print("State: ");
    Log.print(machinestateName[machinestate]);

    Log.print(" Button="); Serial.print(digitalRead(PUSHBUTTON)  ? "not-pressed" : "PRESSed");
    Log.print(" Relay="); Serial.print(digitalRead(RELAY)  ? "ON" : "off");
    Log.print(" GreenLED="); Serial.print(ledstateName[lastgreen]);
    Log.print(" OrangeLED="); Serial.print(ledstateName[lastorange]);
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
      Log.printf("Connection to SSID:%s for 10 seconds now -- Rebooting...\n", WiFi.SSID().c_str());
      delay(500);
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

#ifdef HASRFID
  if (machinestate >= WAITINGFORCARD && millis() - laststatechange > 1500) {
    if (checkTagReader()) {
      laststatechange = millis();
      if (machinestate >= ENERGIZED)
        machinestate = WAITINGFORCARD;
      else
        machinestate = CHECKING;
    }
  }
#endif
}
