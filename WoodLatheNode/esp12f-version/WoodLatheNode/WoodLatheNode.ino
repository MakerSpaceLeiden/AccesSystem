// With the new OTA code -- All ESPs have
// enough room for the code -- though still need
// Over 328kB free to actually use it.
//
#define OTA yes

// Allow the unit to go into AP mode for reconfiguration
// if no wifi network is found.
//
//
#define CONFIGAP

// Comment out to reduce debugging output. Note that most key
// debugging is only visible on the serial port.
//
// #define DEBUG  yes

// When SYSLOGPORT is set - (debug) messages are also
// sent out to the networks broadcast address (guessed from the
// netmask; not picked up from DHCP).
//
// #define SYSLOGPORT (514)


#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

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

#include <PubSubClient.h>
#include <SPI.h>
#include <MFRC522.h>
#include <EEPROM.h>
#include <FS.h>
#include <ArduinoJson.h>
#include <sha256.h>

#define BUILD  __FILE__ " " __DATE__ " " __TIME__

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

#ifndef CONFIGAP
const char ssid[34] = WIFI_NETWORK ;
const char wifi_password[34] = WIFI_PASSWD;
#endif

const char mqtt_server[34] = "not-yet-cnf-mqtt";
const uint16_t mqtt_port = 1883;

// MQTT topics are constructed from <prefix> / <dest> / <sender>
//
const char mqtt_topic_prefix[MAX_TOPIC] = "not-yet-cnf-prefix";
const char moi[MAX_NAME] = "not-yet-cnf-nodename";    // Name of the sender
const char machine[MAX_NAME] = "not-yet-cnf-machinename";
const char master[MAX_NAME] = "not-yet-cnf-mastername";    // Destination for commands

const char *logpath = "log";       // Destination for human readable text/logging info.

// Password - specific for above 'moi' node name; and the name of the
// machine we control.
//
#ifndef ACNODE_PASSWD
#define ACNODE_PASSWD "unset"
#endif

const char passwd[MAX_NAME] = ACNODE_PASSWD;

// Enduser visible Timeouts
//
const unsigned int   IDLE_TO        = (20 * 60 * 1000); // Auto disable/off after 20 minutes.
const unsigned int   CHECK_TO       = (3500); // Wait up to 3.5 second for result of card ok check.

// Wiring of current tablesaw/auto-dust control note.
//
const uint8_t PIN_RFID_SS    = 2;
const uint8_t PIN_RFID_RST   = 16;
// The RFID reader itself is connected to the
// hardwired MISO/MOSI and CLK pins (12, 13, 14)

const uint8_t PIN_LED        = 0; // red led to ground - led mounted inside start button.
const uint8_t PIN_POWER      = 15; // pulled low when not in use (part of the ESP8266 boot select).
const uint8_t PIN_HALL       = A0;

// While we avoid using #defines, as per https://www.arduino.cc/en/Reference/Define, in above - in below
// case - the compiler was found to procude better code if done the old-fashioned way.
//
#ifdef DEBUG
#define Debug Serial
#else
#define Debug if(0) Serial
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
char lasttag[MAX_TAG_LEN * 4];      // 3 diigt byte and a dash or terminating \0. */
unsigned long lasttagbeat;          // Timestamp of last (valid) swipe.
unsigned long beatCounter = 0;      // My own timestamp - manually kept due to SPI timing issues.

// Quick 'tee' class - that sends all 'serial' port data also to the MQTT bus - to the 'log' topic
// if such is possible/enabled.
//

class Log : public Print {
  public:
    void begin(const char * prefix, int speed);
    virtual size_t write(uint8_t c);
  private:
#ifdef SYSLOGPORT
    WiFiUDP syslog;
#endif
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
#ifdef SYSLOGPORT
  udp.begin(localPort);
#endif
  return;
}

size_t Log::write(uint8_t c) {
  size_t r = Serial.write(c);

  if (c >= 32)
    logbuff[ at++ ] = c;

  if (c != '\n' && at <= sizeof(logbuff) - 1)
    return r;

  logbuff[at++] = 0;
  if (client.connected()) {
    client.publish(logtopic, logbuff);
  };

#ifdef SYSLOGPORT
  if (WiFi.status() == WL_CONNECTED) {
    uint32_t bcast = WiFi.localIP();
    uint32_t mask = WiFi.subnetMask();
    IPAddress bcastAddress = IPAddress(bcast & mask);

    syslog.beginPacket(bcastAddress, SYSLOGPORT);
    syslog.write(moi);
    syslog.write(" ");
    syslog.write(logbuff, at);
    syslog.endPacket();
  }
#endif
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
      pinMode(PIN_LED, OUTPUT);
      digitalWrite(PIN_LED, 0);
      break;
    case LED_FAST:
      analogWriteFreq(3);
      analogWrite(PIN_LED, PWMRANGE / 2);
      break;
    case LED_ON:
      analogWrite(PIN_LED, PWMRANGE - 1);
      pinMode(PIN_LED, OUTPUT);
      digitalWrite(PIN_LED, 1);
      break;
  }
}

#ifdef CONFIGAP
//flag for saving data
bool shouldSaveConfig = false;

//callback notifying us of the need to save config
void saveConfigCallback () {
  shouldSaveConfig = true;
}
#endif

void saveSetup() {
  DynamicJsonBuffer jsonBuffer;
  JsonObject& json = jsonBuffer.createObject();

  char mqtt_port_buff[32];
  snprintf(mqtt_port_buff, sizeof(mqtt_port_buff), "%d", mqtt_port);

  json["mqtt_server"] = mqtt_server;
  json["mqtt_port"] = mqtt_port_buff;
  json["node_name"] = moi;
  json["machine"] = machine;
  json["master"] = master;
  json["topic_prefix"] = mqtt_topic_prefix;
  json["passwd"] = passwd;

  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile)
    return;

#ifdef DEBUG
  json.printTo(Debug);
#endif  
  json.printTo(configFile);
  configFile.close();
}

bool loadSetup() {
  if (!SPIFFS.exists("/config.json")) {
    Log.println("No json config file");
    return false;
  }
  File configFile = SPIFFS.open("/config.json", "r");

  if (!configFile) {
    Log.println("Json config file unreadable");
    return false;
  }
  size_t size = configFile.size();
  // Allocate a buffer to store contents of the file.
  std::unique_ptr<char[]> buf(new char[size]);

  configFile.readBytes(buf.get(), size);

  DynamicJsonBuffer jsonBuffer;
  JsonObject& json = jsonBuffer.parseObject(buf.get());

  json.printTo(Log);

  if (!json.success()) {
    Log.println("Json file could not be parsed.");
    return false;
  }

  if (!json.containsKey("mqtt_server")) {
    Log.println("Invalid/missing mqtt_server value in json file");
    return false;
  }
  strncpy((char *)mqtt_server, json["mqtt_server"],  sizeof(mqtt_server));

  if (!json.containsKey("mqtt_port")) {
    Log.println("Invalid / missing mqtt_port value in json file");
    return false;
  }

  *(uint16_t*)&mqtt_port = atoi(json["mqtt_port"]);

  if (!json.containsKey("node_name")) {
    Log.println("Invalid / missing node_name value in json file");
    return false;
  }
  strncpy((char *)moi, json["node_name"],  sizeof(moi));

  if (!json.containsKey("node_name")) {
    Log.println("Invalid / missing node_name value in json file");
    return false;
  }
  strncpy((char *)machine, json["node_name"],  sizeof(machine)); // the same for this unit.

  if (!json.containsKey("master")) {
    Log.println("Invalid / missing master value in json file");
    return false;
  };
  strncpy((char *)master, json["master"],  sizeof(master)); // the same for this unit.

  if (!json.containsKey("topic_prefix")) {
    Log.println("Invalid / missing topic_prefix value in json file");
    return false;
  };
  strncpy((char *)mqtt_topic_prefix, json["topic_prefix"],  sizeof(mqtt_topic_prefix));

  if (!json.containsKey("passwd")) {
    Log.println("Invalid / missing passwd value in json file");
    return false;
  }
  strncpy((char *)passwd, json["passwd"],  sizeof(passwd));

  return true;
  return strlen(mqtt_server) &&
         strlen(moi) &&
         strlen(mqtt_topic_prefix) &&
         strlen(passwd) &&
         mqtt_port > 0 &&
         strlen(master) &&
         strlen(machine);
}

void setup() {
  pinMode(PIN_POWER, OUTPUT);
  digitalWrite(PIN_POWER, 0);

  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, 1);

#ifdef PIN_OPTO_ENERGIZED
  pinMode(PIN_OPTO_ENERGIZED, INPUT);
#endif
#ifdef PIN_OPTO_OPERATOR
  pinMode(PIN_OPTO_OPERATOR, INPUT);
#endif

  setRedLED(LED_FAST);

#ifdef DEBUG
  uint32_t realSize = ESP.getFlashChipRealSize();
  uint32_t ideSize = ESP.getFlashChipSize();
  FlashMode_t ideMode = ESP.getFlashChipMode();

  Serial.printf("Flash real id:   %08X\n", ESP.getFlashChipId());
  Serial.printf("Flash real size: %u\n\n", realSize);

  Serial.printf("Flash ide  size: %u\n", ideSize);
  Serial.printf("Flash ide speed: %u\n", ESP.getFlashChipSpeed());
  Serial.printf("Flash ide mode:  %s\n", (ideMode == FM_QIO ? "QIO" : ideMode == FM_QOUT ? "QOUT" : ideMode == FM_DIO ? "DIO" : ideMode == FM_DOUT ? "DOUT" : "UNKNOWN"));

  if (ideSize != realSize) {
    Serial.println("Flash Chip configuration wrong!\n");
  } else {
    Serial.println("Flash Chip configuration ok.\n");
  }
#endif

  if (!SPIFFS.begin() || !SPIFFS.exists("/config.json")) {
    Log.println("Damaged flash file system; reformatting");
    SPIFFS.format();
  };

  bool validCnf = loadSetup();

  if (!validCnf)
    Log.println("Config read failed");
  else
    Log.println("Config read OK");

  Log.begin(mqtt_topic_prefix, 115200);
  Log.println("\n\n\nBuild: " BUILD);

  WiFi.mode(WIFI_STA);

#ifndef CONFIGAP
  WiFi.begin(ssid, wifi_password);
#else
  WiFi.begin();
#endif

  WiFi.SSID();
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start < 10000)) {
    delay(100);
  };
  if (WiFi.status() != WL_CONNECTED)
    Log.println("Not connected yet.");


#ifdef DEBUG
  Debug.println("SPI File System: ");
  Dir dir = SPIFFS.openDir("/");
  while (dir.next()) {
    String fileName = dir.fileName();
    size_t fileSize = dir.fileSize();
    Debug.printf("FS File: %s, size: %d\n", fileName.c_str(), fileSize);
  }
  Debug.printf("\n");
#endif

  if (WiFi.status() != WL_CONNECTED) {
    Log.print("Connection to <"); Log.print(WiFi.SSID()); Log.println("> failed");
    validCnf = false;
  }

#ifdef CONFIGAP
  if (!validCnf) {
    Log.print("Going into AP mode\n");

    WiFiManager wifiManager;

    char mqtt_port_buff[5];
    char passwd_buff[MAX_NAME];
    snprintf(mqtt_port_buff, sizeof(mqtt_port_buff), "%d", mqtt_port);
    passwd_buff[0] = 0; // force user to (re)set the password; rather than reveal anything.

    WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, sizeof(mqtt_server));
    WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port_buff, sizeof(mqtt_port_buff));
    WiFiManagerParameter custom_node("node", "node name", moi, sizeof(moi));
    WiFiManagerParameter custom_master("master", "master", master, sizeof(master));
    WiFiManagerParameter custom_prefix("topic_prefix", "topix prefix", mqtt_topic_prefix, sizeof(mqtt_topic_prefix));
    WiFiManagerParameter custom_passwd("passwd", "shared secret", passwd_buff, sizeof(passwd_buff));

    wifiManager.addParameter(&custom_mqtt_server);
    wifiManager.addParameter(&custom_mqtt_port);
    wifiManager.addParameter(&custom_node);
    wifiManager.addParameter(&custom_master);
    wifiManager.addParameter(&custom_prefix);
    wifiManager.addParameter(&custom_passwd);

    wifiManager.setSaveConfigCallback(saveConfigCallback);

    char apname[128];
    snprintf(apname,sizeof(apname),"%s Config", moi);
    wifiManager.autoConnect(apname);

#if DEBUG
    // we are not passing a name - so that each ESP generates its own unique name;
    if (!wifiManager.startConfigPortal())
#else
    if (!wifiManager.startConfigPortal("ACNode Config"))
#endif
    {
      Serial.println("failed to connect and hit timeout - rebooting");
      delay(1000);
      //reset and try again, or maybe put it to deep sleep
      ESP.reset();
      delay(5000);
    }

    if (shouldSaveConfig) {
      strncpy((char *)mqtt_server, custom_mqtt_server.getValue(), sizeof(mqtt_server));
      // char portptr[32];
      // strncpy(portptr, custom_mqtt_port.getValue(), sizeof(portptr));
      *(uint16_t *)&mqtt_port = atoi(custom_mqtt_port.getValue());
      strncpy((char *)moi, custom_node.getValue(), sizeof(moi));
      strncpy((char *)machine, custom_node.getValue(), sizeof(machine));
      strncpy((char *)master, custom_master.getValue(), sizeof(master));
      strncpy((char *)mqtt_topic_prefix, custom_prefix.getValue(), sizeof(mqtt_topic_prefix));
      strncpy((char*)passwd, custom_passwd.getValue(), sizeof(passwd));

      Log.println("Saving new config.");
      saveSetup();
      if (!loadSetup()) {
        Log.println("Reloading failed - rebooting..");
        ESP.restart();
      }
    }
#else
  Log.println("Rebooting...");
  ESP.restart();
#endif
  }

  Log.println("Wifi connected.");
#ifdef OTA
  ArduinoOTA.setPort(8266);
  ArduinoOTA.setHostname(moi);
  Log.println("OTA Enabled.");
#ifdef OTA_PASSWD
  ArduinoOTA.setPassword((const char *)OTA_PASSWD);
  Log.println("OTA Password set.");
#endif

  ArduinoOTA.onStart([]() {
    Log.println("OTA process started.");
    digitalWrite(PIN_POWER, 0);
  });
  ArduinoOTA.onEnd([]() {
    Log.println("OTA process completed. Resetting.");
    digitalWrite(PIN_POWER, 0);
    digitalWrite(PIN_LED, 0);
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Log.printf("%c%c%c%cProgress:%u%% ", 27, '[', '1', 'G', (progress / (total / 100)));
    digitalWrite(PIN_LED, !digitalRead(PIN_LED));
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

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqtt_callback);

  machinestate = WAITINGFORCARD;
  setRedLED(LED_ON);
}

const char * state2str(int state) {
#if 1 // we have the memory on an ESP8266
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
      return "the username / password were rejected";
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

bool isUsingPower() {
  static bool laststate = false;
  static unsigned long lastTime = 0;

  // Avoid measuring too often as to not slow our
  // run loop too much.
  //
  if (millis() - lastTime > 1200) {
    unsigned long from = millis();
    unsigned long i = 0, sum = 0, sd = 0;
    //
    // ACS 712 Hall sensor
    // http://www.allegromicro.com/~/media/Files/Datasheets/ACS712-Datasheet.ashx
    //
    // Our hall sensor provides a voltage swing around 2.5 volt (mapped by two resistors into
    // the 0..1 volt range). We are interested in the currents at the 'peaks' of the 50hz sine.
    // So we calculate something of a running standard deviation; as to focus on these
    // fluctuations rather than on the average (of 2.5 Volt, or around 500 on readout).
    //
    // Measure 8-9 full 50hz sine cycles.
    while (millis() - from < 90) {
      unsigned int v = analogRead(PIN_HALL);
      i++; sum += v;
      v -= sum / i; // delta from the curerent mean.
      sd += v * v / i;
    }
    laststate = (sd > 20);
    lastTime = millis();
  }
  return laststate;
}

const unsigned char * hmacBytes(const char *passwd, const char * beatAsString, const char * topic, const char *payload) {

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

  Debug.printf("Sending %s:%s\n", topic, msg);
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
  Debug.printf("Attempting MQTT connection (State : %s)\n",state2str(client.state()));

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
#ifdef DEBUG
    send("Warning: DEBUG is ON");
#endif
  } else {
    Log.print("Reconnect failed : ");
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

#ifdef DEBUG
  if (!strcmp("wack", payload)) {
    SPIFFS.remove("/config.json");
    Log.println("config file deleted.");
    return;
  }
  if (!strcmp("json", payload)) {
    File configFile = SPIFFS.open("/config.json", "r");
    if (!configFile) {
      Log.println("No / config.json");
      return;
    };
    size_t size = configFile.size();
    // Allocate a buffer to store contents of the file.
    std::unique_ptr<char[]> buf(new char[size + 1]);
    configFile.readBytes(buf.get(), size);
    char * p = buf.get();
    p[size] = '\0';
    Log.println(p);
    return;
  }
#endif
  if (!strcmp("lsspiffs", payload)) {
    Dir dir = SPIFFS.openDir("/");
    while (dir.next()) {
      Log.printf("FS File: %s, size: %d\n",
                 dir.fileName().c_str(), dir.fileSize());
    }
    return;
  };
  if (!strcmp("current", payload)) {
    unsigned long from = millis();
    unsigned long i = 0, a = 0, sd = 0;
    while (millis() - from < 190) {
      unsigned int v = analogRead(PIN_HALL);
      i++; a += v;
      v -= a / i; // delta from the curerent mean.
      sd += v * v / i;
    }
    Log.printf("Current: %d - %d (%d #)\n", analogRead(PIN_HALL), sd, i);
    return;
  }

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
#ifdef DEBUG
    Log.printf("Invalid signature (%s != %s) - ignoring.", hmac2, hmac);
#else
    Log.println("Invalid signature - ignoring.");
#endif

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
    Log.print("Good message -- but message ignored as beat is too far off ("); Log.print(delta); Log.println(" seconds).");
    return;
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

  char filename[128];
  snprintf(filename, sizeof(filename), "/%s.tag", lasttag);

  if (!strncmp("approved", rest, 8) || !strncmp("energize", rest, 8)) {
    File fh = SPIFFS.open(filename, "w");
    if (fh) {
      fh.println(lasttagbeat);
      fh.close();
    } else {
      SPIFFS.remove(filename);
    }
    machinestate = POWERED;
    return;
  }

  if (!strncmp("denied", rest, 6) || !strncmp("unknown", rest, 7)) {
    Log.println("Flash LEDS");
    SPIFFS.remove(filename);
    setRedLED(LED_FAST);
    delay(1000);
    setRedLED(LED_OFF);
    machinestate = WAITINGFORCARD;
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
  int r = 0;

#ifdef PIN_OPTO_ENERGIZED
  int relayState = digitalRead(PIN_OPTO_ENERGIZED);
  if (relayState) r |= 1;
#endif

#ifdef PIN_OPTO_OPERATOR
  int operatorSwitchState = digitalRead(PIN_OPTO_OPERATOR);
  if (operatorSwitchState) r |= 2;
#endif

#if 0
  switch (r) {
    case 0: // On/off switch 'on' - blocking energizing.
      if (machinestate == RUNNING) {
        send("event stop - pressed");
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
        send("event stop - pressed");
        machinestate = WAITINGFORCARD;
      }
      if (machinestate == WRONGFRONTSWITCHSETTING) {
        send("event frontswitchokagain");
        machinestate = WAITINGFORCARD;
      };
      break;
    case 3: // Relay energized, but not running.
      if (machinestate == POWERED) {
        send("event start - pressed");
        machinestate = ENERGIZED;
      };
      if (machinestate == RUNNING) {
        send("event halted");
        machinestate = ENERGIZED;
      } else if (machinestate < ENERGIZED && machinestate > NOCONN) {
        static int last_spur_detect = 0;
        if (millis() - last_spur_detect > 500) {
          send("event spuriousbuttonpress ? ");
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
#endif
  if (isUsingPower()) {
    if (machinestate != RUNNING && machinestate > WAITINGFORCARD) {
      send("event running");
      machinestate = RUNNING;
    }
  } else {
    if (machinestate == RUNNING) {
      send("event halted");
      machinestate = ENERGIZED;
      laststatechange = millis(); // reset the timeout idler.
    };
  }
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
      setRedLED(((tock % 100) > 3) ? LED_OFF : LED_ON);
      break;
    case CHECKING:
      setRedLED(LED_FAST);
      if (millis() - laststatechange > CHECK_TO) {
        setRedLED(LED_OFF);
        Log.printf("Delta=%d milliSeconds / Returning to waiting for card - no response.\n", millis() - laststatechange);
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
        Log.printf("Machine not used for more than %d minutes; revoking access.\n", IDLE_TO / 1000 / 60);
        machinestate = WAITINGFORCARD;
      } else
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
    Serial.print(" 1 = "); Serial.print(v1);
    Serial.print(" 2 = "); Serial.print(v2);
    Serial.print(" red = "); Serial.print(red);
    Serial.print(" P = "); Serial.print(relayenergized);
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

#ifdef FAKE_PYTHON_CRC
  uint8_t crc = 0;
  for (int i = 0; i < uid.size; i++)
    crc ^= uid.uidByte[i];
  uid.uidByte[uid.size] = crc;
  uid.size++;
#endif

  lasttag[0] = 0;
  for (int i = 0; i < uid.size; i++) {
    char buff[6];
    snprintf(buff, sizeof(buff), "%s%d", i ? "-" : "", uid.uidByte[i]);
    strcat(lasttag, buff);
  }
  lasttagbeat = beatCounter;

  char beatAsString[ MAX_BEAT ];
  snprintf(beatAsString, sizeof(beatAsString), BEATFORMAT, beatCounter);

  // For some reason - we occasionally see strlen(passwd); i.e. on the static
  // defined global variable return 9 rather than 14 chars. This 'solves' that
  // issue. But we have no idea why. It seems compiler optimize flag related.
  //
  char _passwd[MAX_NAME]; strcpy(_passwd, passwd);

  Sha256.initHmac((const uint8_t *)_passwd, strlen(_passwd));
  Sha256.print(beatAsString);
  Sha256.write(uid.uidByte, uid.size);
  const char * tag_encoded = hmacToHex(Sha256.resultHmac());

  char msg[MAX_MSG];
  snprintf(msg, sizeof(msg), "energize %s %s %s", moi, machine, tag_encoded);
  send(msg);

  return 1;
}
void loop() {

#ifdef DEBUG
  static int last_debug = 0, last_debug_state = -1;
  if (millis() - last_debug > 5000 || last_debug_state != machinestate) {
    Log.print("State: ");
    Log.print(machinestateName[machinestate]);

#ifdef PIN_OPTO_ENERGIZED
    int relayState = digitalRead(PIN_OPTO_ENERGIZED);
    Log.print(" optoRelay = "); Log.print(relayState);
#endif

#ifdef  PIN_OPTO_OPERATOR
    int operatorSwitchState = digitalRead(PIN_OPTO_OPERATOR);
    Log.print(" optoOerator = "); Log.print(operatorSwitchState);
#endif

    Log.print(" LED="); Log.print(lastred);

    Log.print(" millisSinceLastChange="); Log.print(millis() - laststatechange);
    Log.print(" maxIdle="); Log.print(IDLE_TO);

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
    if (machinestate <= WAITINGFORCARD) {
      machinestate = NOCONN;
      if ( millis() - last_wifi_ok > 10000) {
        Log.println("Connection dead for 10 seconds now -- Rebooting...");
        ESP.restart();
      }
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
      Debug.print("No MQTT connection : ");
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

  // We try to avoid reading too often; as this slows down the main loop.
  // We ought to wire up the IRQ pin - and do this asynchroneously.
  //
  static unsigned long lastread = 0;
  if (machinestate >= WAITINGFORCARD && millis() - laststatechange > 1500 && millis() - lastread > 500) {
    lastread = millis();
    if (checkTagReader()) {
      char filename[128];
      snprintf(filename, sizeof(filename), "/%s.tag", lasttag);
      if (SPIFFS.exists(filename)) {
        machinestate = POWERED;
        Log.println("Powered on based on cached OK.");
      } else if (machinestate >= ENERGIZED)
        machinestate = WAITINGFORCARD;
      else
        machinestate = CHECKING;
      laststatechange = millis();
    }
  }
}
