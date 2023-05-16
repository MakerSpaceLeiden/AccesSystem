// With the new OTA code -- All ESPs have
// enough room for the code -- though still need
// Over 328kB free to actually use it.
//
#define BUILD  __FILE__ " " __DATE__ " " __TIME__
#define OTA
// #define DEBUG  yes
// Run without the hardware.
#define FAKEMODE

#include <ESP8266WiFi.h>

#ifdef OTA
#include <WiFiUdp.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#endif

#include <PubSubClient.h>
#include <SPI.h>
#include <MFRC522.h>

// Hardware random generator.
#include <esp8266-trng.h>

// For the HMAC of the SIG/1.0 protocol
#include <sha256.h>

// Curve/Ed25519 related (and SIG/2.0 protocol)
#include <Crypto.h>
#include <Curve25519.h>
#include <Ed25519.h>
#include <RNG.h>
#include <AES.h>
#include <CTR.h>
#include <CBC.h>

#include <base64.hpp>

#ifdef DEBUG
#include <GDBStub.h>
#endif

#include <EEPROM.h>

#if MQTT_MAX_PACKET_SIZE < 256
#error "You will need to increase te MQTT_MAX_PACKET_SIZE size a bit in PubSubClient.h"
#endif

#include "../../../../.passwd.h"

const char* ssid = WIFI_NETWORK;
const char* wifi_password = WIFI_PASSWD;
const char* mqtt_server = "space.makerspaceleiden.nl";

// MQTT topics are constructed from <prefix> / <dest> / <sender>
//
// const char *mqtt_topic_prefix = "makerspace/ac";
const char *mqtt_topic_prefix = "test";
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
const unsigned int   AUTOOFF_TO     = (2 * IDLE_TO);

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

// const uint8_t PIN_AUTOOFF 9;

// The RFID reader itself is connected to the
// hardwired MISO/MOSI and CLK pins (12, 13, 14)

#define ED59919_SIGLEN          (64)
#define CURVE259919_KEYLEN      (32)
#define CURVE259919_SESSIONLEN  (CURVE259919_KEYLEN)

#if HASH_LENGTH != CURVE259919_SESSIONLEN
#error SHA256 "hash should be the same size as the session key"
#endif
#if HASH_LENGTH != 32 // AES256::keySize() 
#error SHA256 "hash should be the same size as the encryption key"
#endif

// Data stored persistently (in th
//
typedef struct __attribute__ ((packed)) {
#define EEPROM_VERSION (0x0103)
  uint16_t version;
  uint8_t flags;
  uint8_t spare;

  // Ed25519 key (Curve25519 key in Edwards y space)
  uint8_t node_privatesign[CURVE259919_KEYLEN];
  uint8_t master_publicsignkey[CURVE259919_KEYLEN];

} eeprom_t;
eeprom_t eeprom;

uint8_t node_publicsign[CURVE259919_KEYLEN];

// Curve25519 key (In montgomery x space) - not kept in
// persistent storage as we renew on reboot in a PFS
// sort of 'light' mode.
//
uint8_t node_publicsession[CURVE259919_KEYLEN];
uint8_t node_privatesession[CURVE259919_KEYLEN];
uint8_t sessionkey[CURVE259919_SESSIONLEN];

#define CRYPTO_HAS_PRIVATE_KEYS (1<<0)
#define CRYPTO_HAS_MASTER_TOFU (1<<1)

#define RNG_APP_TAG BUILD
#define RNG_EEPROM_ADDRESS (sizeof(eeprom)+4)

typedef enum {
  SWERROR, OUTOFORDER, NOCONN, // some error - machine disabled.
  WRONGFRONTSWITCHSETTING,    // The switch on the front is in the 'on' setting; this blocks the operation of the on/off switch.
  DENIED,                     // we got a denied from the master -- flash an LED and then return to WAITINGFORCARD
  CHECKING,                   // we are are waiting for the master to respond -- flash an LED and then return to WAITINGFORCARD
  WAITINGFORCARD,             // waiting for card.
  BOOTING,                    // device still booting up.
  NOTOFU,                     // No trust on first use -- not learned keys of the master yet.
  POWERED,                    // Relay powered.
  ENERGIZED,                  // Got the OK; go to RUNNING once the green button at the back is pressed & operator switch is on.
  RUNNING,                    // Running - go to DUSTEXTRACT once the front switch is set to 'off' or to WAITINGFORCARD if red is pressed.
  AUTOOFF,                    // Pseudo state - auto off after a slight idle to allow logging traffic to escape
  NOTINUSE,
} machinestates_t;

const char *machinestateName[NOTINUSE] = {
  "Software Error", "Out of order", "No network",
  "Operator switch in wrong positson",
  "Tag Denied",
  "Checking tag",
  "Waiting for card to be presented",
  "Booting",
  "Awaiting Master TOFU",
  "Relay powered",
  "Energized",
  "Running",
  "Auto shutdown initiated"
};

static machinestates_t laststate;
unsigned long laststatechange = 0;
machinestates_t machinestate;

typedef enum { LED_OFF, LED_FAST, LED_ON, NEVERSET } ledstate;

ledstate lastred = NEVERSET;
ledstate red = NEVERSET;

void mqtt_callback(char* topic, byte* payload_theirs, unsigned int length);
void send(const char * topic, const char * payload);

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
#ifndef GDBSTUB_H
  size_t r = Serial.write(c);
#endif

  if (c >= 32)
    logbuff[ at++ ] = c;
  logbuff[at] = 0;

  if (c == '\n' || at + 2 >= sizeof(logbuff)) {
    if (client.connected())
      send(logtopic, logbuff);
    at = 0;
  }

  return 1;
}
Log Log;

#if 0
#define Log if (0) Log
#define Debug if (0) Log
#else
#ifdef DEBUG
// #define Debug Serial
#define Debug Log
#else
#define Debug if (0) Log
#endif
#endif


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
    case NEVERSET:
    default:
      break;
  }
}

void kickoff_RNG() {
  // Attempt to get a half decent seed soon after boot. We ought to pospone all operations
  // to the run loop - well after DHCP has gotten is into business.
  //
  // Note that Wifi/BT should be on according to:
  //    https://github.com/espressif/esp-idf/blob/master/components/esp32/hw_random.c
  //
  RNG.begin(RNG_APP_TAG, RNG_EEPROM_ADDRESS);

  Sha256.init();
  for (int i = 0; i < 25; i++) {
    uint32_t r = trng(); // RANDOM_REG32 ; // Or esp_random(); for the ESP32 in recent libraries.
    Sha256.write((char*)&r, sizeof(r));
    delay(10);
  };
  RNG.stir(Sha256.result(), 256, 100);

  RNG.setAutoSaveTime(60);
}

void maintain_rng() {
  RNG.loop();

  if (RNG.available(1024 * 4))
    return;

  uint32_t seed = trng();
  RNG.stir((const uint8_t *)&seed, sizeof(seed), 100);
}

#define EEPROM_PRIVATE_OFFSET (0x100)
void load_eeprom() {
  for (size_t adr = 0; adr < sizeof(eeprom); adr++)
    ((uint8_t *)&eeprom)[adr] = EEPROM.read(EEPROM_PRIVATE_OFFSET + adr);
}

void save_eeprom() {
  for (size_t adr = 0; adr < sizeof(eeprom); adr++)
    EEPROM.write(EEPROM_PRIVATE_OFFSET + adr,  ((uint8_t *)&eeprom)[adr]);
  EEPROM.commit();
}

void wipe_eeprom() {
  bzero((uint8_t *)&eeprom, sizeof(eeprom));
  eeprom.version = EEPROM_VERSION;
  save_eeprom();
}

// Ideally called from the runloop - i.e. late once we have at least a modicum of
// entropy from wifi/etc.
//
int setup_curve25519() {
  load_eeprom();

  if (eeprom.version != EEPROM_VERSION) {
    Log.printf("EEPROM Version %04x not understood -- clearing.\n", eeprom.version );
    wipe_eeprom();
  }

  ESP.wdtFeed();
  if (eeprom.flags & CRYPTO_HAS_PRIVATE_KEYS)
    Ed25519::derivePublicKey(node_publicsign, eeprom.node_privatesign);

  if (millis() - laststatechange < 1000)
    return -1;

  Log.println("Generating Curve25519 session keypair");

  ESP.wdtFeed();
  Curve25519::dh1(node_publicsession, node_privatesession);
  bzero(sessionkey, sizeof(sessionkey));

  if (eeprom.flags & CRYPTO_HAS_MASTER_TOFU) {
    Debug.printf("EEPROM Version %04x contains all needed keys and is TOFU to a master with public key\n", eeprom.version);
    return 0;
  }

  ESP.wdtFeed();
  Ed25519::generatePrivateKey(eeprom.node_privatesign);
  ESP.wdtFeed();
  Ed25519::derivePublicKey(node_publicsign, eeprom.node_privatesign);

  eeprom.flags |= CRYPTO_HAS_PRIVATE_KEYS;

  save_eeprom();
  return 0;
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_POWER, OUTPUT);
  digitalWrite(PIN_POWER, 0);

  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, 1);

  pinMode(PIN_OPTO_ENERGIZED, INPUT);
  pinMode(PIN_OPTO_OPERATOR, INPUT);

  setRedLED(LED_FAST);

  EEPROM.begin(1024);

  Log.begin(mqtt_topic_prefix, 115200);
  Log.println("\n\n\nBuild: " BUILD);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, wifi_password);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start < 5000)) {
    delay(100);
  };

  if (WiFi.status() != WL_CONNECTED) {
    Log.printf("Connection to <%s> failed, rebooting\n", ssid);
    ESP.restart();
  }

  Log.printf("Wifi connected to <%s> OK.\n", ssid);
#ifdef OTA
  ArduinoOTA.setPort(8266);
  ArduinoOTA.setHostname(moi);
  ArduinoOTA.setPassword((const char *)OTA_PASSWD);

  ArduinoOTA.onStart([]() {
    Log.println("OTA process started. Wiping keys.");
    wipe_eeprom();
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
      Log.printf("OTA: Error: %s\n", error);
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

  kickoff_RNG();

  machinestate = BOOTING;
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
    Serial.println("Out of memory");
    Serial.flush();
#ifdef DEBUG
    *((int*)0) = 0;
#else
    ESP.restart();//    ESP.reset();
#endif
  };
  // We append at the very end -- this preserves order -and- allows
  // us to add things to the queue while in something works on it.
  //
  publish_rec_t ** p = &publish_queue;
  while (*p) p = &(*p)->nxt;
  *p = rec;
}

void publish_loop() {
  if (!publish_queue)
    return;
  publish_rec_t * rec = publish_queue;
  publish(rec->topic, rec->payload);

  publish_queue = rec->nxt;
  free(rec->topic);
  free(rec->payload);
  free(rec);
}

const char * hmacAsHex(const char *passwd, const char * beatAsString, const char * topic, const char *payload)
{
  const unsigned char * hmac = hmacBytes(passwd, beatAsString, topic, payload);
  return hmacToHex(hmac);
}

// Note - doing any logging/publish in below is 'risky' - as
// it may lead to an endless loop.
//
void publish(const char *topic, const char *payload) {
  char msg[ MAX_MSG];
  const char *vs = "?.??";

  char beat[MAX_BEAT];
  snprintf(beat, sizeof(beat), BEATFORMAT, beatCounter);

  if (eeprom.flags & CRYPTO_HAS_PRIVATE_KEYS) {
    vs = "2.0";
    uint8_t signature[ED59919_SIGLEN];

    char tosign[MAX_MSG];
    size_t len = snprintf(tosign, sizeof(tosign), "%s %s", beat, payload);

    ESP.wdtFeed();
    Ed25519::sign(signature, eeprom.node_privatesign, node_publicsign, tosign, len);

    char sigb64[ED59919_SIGLEN * 2]; // plenty for an HMAC and for a 64 byte signature.
    encode_base64(signature, sizeof(signature), (unsigned char *)sigb64);

    snprintf(msg, sizeof(msg), "SIG/%s %s %s", vs, sigb64, tosign);
  } else {
    vs = "1.0";
    const char * sig  = hmacAsHex(passwd, beat, topic, payload);
    snprintf(msg, sizeof(msg), "SIG/%s %s %s %s", vs, sig, beat, payload);
  }

  client.publish(topic, msg);
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
  if (*q)
    return q;
  return NULL;
}

void send_helo(const char * helo) {
  char buff[MAX_MSG];

  IPAddress myIp = WiFi.localIP();
  snprintf(buff, sizeof(buff), "%s %d.%d.%d.%d", helo, myIp[0], myIp[1], myIp[2], myIp[3]);

  if (eeprom.flags & CRYPTO_HAS_PRIVATE_KEYS) {
    char b64[128];

    // Add ED25519 signing/non-repudiation key
    strncat(buff, " ", sizeof(buff));
    encode_base64((unsigned char *)node_publicsign, sizeof(node_publicsign), (unsigned char *)b64);
    strncat(buff, b64, sizeof(buff));

    // Add Curve25519 session/confidentiality key
    strncat(buff, " ", sizeof(buff));
    encode_base64((unsigned char *)(node_publicsession), sizeof(node_publicsession), (unsigned char *)b64);
    strncat(buff, b64, sizeof(buff));
  }

  Log.println(buff);
  send(NULL, buff);
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

    send_helo((char *)"announce");
  } else {
    Log.print("failed : ");
    Log.println(state2str(client.state()));
  }
}

void mqtt_callback(char* topic, byte * payload_theirs, unsigned int length) {
  char payload[MAX_MSG];

  if (length >= sizeof(payload))
    length = sizeof(payload) - 1;

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

#define SEP(tok, err) char *  tok = strsepspace(&p); if (!tok) { Log.print("Malformed/missing " err ": " ); Log.println(p); return; }
#define B64D(base64str, bin, what) { \
    if (decode_base64_length((unsigned char *)base64str) != sizeof(bin)) { \
      Log.printf("Wrong length " what " (expected %d, got %d/%s) - ignoring\n", sizeof(bin), decode_base64_length((unsigned char *)base64str), base64str); \
      return; \
    }; \
    decode_base64((unsigned char *)base64str, bin); \
  }

  SEP(version, "SIG header");
  SEP(sig, "HMAC");

  uint8_t * signkey = eeprom.master_publicsignkey;

  char signed_payload[MAX_MSG];
  strncpy(signed_payload, p, sizeof(signed_payload));
  SEP(beat, "BEAT");
  char * rest = p;

  bool newtofu = false;
  bool newsession = false;

  unsigned char pubencr_tmp[CURVE259919_KEYLEN];

  if (!strncmp(version, "SIG/1", 5)) {
    const char * hmac2 = hmacAsHex(passwd, beat, topic, p);
    if (strcasecmp(hmac2, sig)) {
      Log.println("Invalid HMAC signature - ignoring.");
      return;
    }
  } else if (!strncmp(version, "SIG/2", 5)) {
    unsigned char pubsign_tmp[CURVE259919_KEYLEN];
    uint8_t signature[ED59919_SIGLEN];

    bool tofu = (eeprom.flags & CRYPTO_HAS_MASTER_TOFU) ? true : false;

    B64D(sig, signature, "Ed25519 signature");
    SEP(cmd, "command");

    if (strcmp(cmd, "welcome") == 0  || strcmp(cmd, "announce") == 0) {
      newsession = true;

      SEP(host_ip, "IP address");
      SEP(master_publicsignkey_b64, "B64 public signing key");
      SEP(master_publicencryptkey_b64, "B64 public encryption key");

      B64D(master_publicsignkey_b64, pubsign_tmp, "Ed25519 public key");
      B64D(master_publicencryptkey_b64, pubencr_tmp, "Curve25519 public key");

      if (tofu) {
        if (memcmp(eeprom.master_publicsignkey, pubsign_tmp, sizeof(eeprom.master_publicsignkey))) {
          Log.println("Sender has changed its public signing key(s) - ignoring.");
          return;
        }
        Debug.println("Known Ed25519 signature on message.");
      } else {
        Debug.println("Unknown Ed25519 signature on message - giving the benefit of the doubt.");
        signkey = pubsign_tmp;

        // We are not setting the TOFU flag in the EEPROM yet, as we've not yet checked
        // for reply by means of the beat.
        //
        newtofu = true;
      }

      if (!tofu && !newtofu) {
        Log.println("Cannot (yet) validate signature - ignoring while waiting for welcome/announce");
        return;
      };
    };
    ESP.wdtFeed();
    if (!Ed25519::verify(signature, signkey, signed_payload, strlen(signed_payload))) {
      Log.println("Invalid Ed25519 signature on message - ignoring.");
      return;
    };
  } else {
    Log.print("Unknown signature format <"); Log.print(version); Log.println("> - ignoring.");
    return;
  };

  unsigned long  b = strtoul(beat, NULL, 10);
  if (!b) {
    Log.print("Unparsable beat - ignoring.");
    return;
  };

  unsigned long delta = llabs((long long) b - (long long)beatCounter);

  // otherwise - only accept things in a 4 minute window either side.
  //
  if ((beatCounter < 3600) || (delta < 120)) {
    beatCounter = b;
    if (delta > 10) {
      Log.print("Adjusting beat by "); Log.print(delta); Log.println(" seconds.");
    } else if (delta) {
      Debug.print("Adjusting beat by "); Debug.print(delta); Debug.println(" seconds.");
    }
  } else {
    Log.print("Good message -- but beats ignored as they are too far off ("); Log.print(delta); Log.println(" seconds).");
    return;
  };

  if (newtofu) {
    Debug.println("TOFU for Ed25519 key of master, stored in persistent store..");
    memcpy(eeprom.master_publicsignkey, signkey, sizeof(eeprom.master_publicsignkey));

    eeprom.flags |= CRYPTO_HAS_MASTER_TOFU;
    save_eeprom();
  }
  if (newsession) {
    // Allways allow for the updating of session keys. On every welcome/announce. Provided that
    // the signature matched.

    // We need to copy the key as 'dh2()' will wipe its inputs as a side effect of the calculation.
    // Which usually makes sense -- but not in our case - as we're async and may react to both
    // a welcome and an announce -- so regenerating it on both would confuse matters.
    //
    // XX to do - consider regenerating it after a welcome; and go through replay attack options.
    //
    uint8_t tmp_private[CURVE259919_KEYLEN];

    memcpy(sessionkey, pubencr_tmp, sizeof(sessionkey));
    memcpy(tmp_private, node_privatesession, sizeof(tmp_private));
    ESP.wdtFeed();
    Curve25519::dh2(sessionkey, tmp_private);

    ESP.wdtFeed();
    Sha256.init();
    Sha256.write((char*)&sessionkey, sizeof(sessionkey));
    memcpy(sessionkey, Sha256.result(), sizeof(sessionkey));

#if 0
    unsigned char key_b64[128];
    encode_base64(sessionkey, sizeof(sessionkey), key_b64);
    Serial.print("RAW session key: "); Serial.println((char *)key_b64);
    encode_base64(sessionkey, sizeof(sessionkey), key_b64);
    Serial.print("HASHed session key: "); Serial.println((char *)key_b64);
#endif

    Log.printf("(Re)calculated session key - slaved to %s\n", topic);

    eeprom.flags |= CRYPTO_HAS_MASTER_TOFU;
  };

  if (!strncmp("announce", rest, 8)) {
    Debug.println("pre welcome in handler");
    ESP.wdtFeed();
    send_helo((char *)"welcome");
    ESP.wdtFeed();
    Debug.println("post welcome in handler");
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

  if (!strncmp("state", rest, 4)) {
    char buff[MAX_MSG];
    snprintf(buff, sizeof(buff), "state %d %s", machinestate, machinestateName[machinestate]);
    send(NULL, buff);
    return;
  }

  if (!strncmp("revealtag", rest, 9)) {
    if (eeprom.flags & CRYPTO_HAS_PRIVATE_KEYS) {
      Log.println("Ignoring reveal command. Already passed encrypted.");
      return;
    };

    if (b < lasttagbeat) {
      Log.println("Asked to reveal a tag I no longer have a record off, ignoring.");
      return;
    };
    char buff[MAX_MSG];
    snprintf(buff, sizeof(buff), "lastused %s", lasttag);
    send(NULL, buff);
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
    send(NULL, "event outoforder");
    return;
  }

  Log.printf("Do not know what to do with <%s>, ignoring.\n", rest);
  return;
}

unsigned int tock;
void handleMachineState() {
  tock++;

#ifdef FAKEMODE
  int relayState = 1;
  int operatorSwitchState = 0;
#else
  int relayState = digitalRead(PIN_OPTO_ENERGIZED);
  int operatorSwitchState = digitalRead(PIN_OPTO_OPERATOR);
#endif

  int r = 0;
  if (relayState) r |= 1;
  if (operatorSwitchState) r |= 2;

  switch (r) {
    case 0: // On/off switch 'on' - blocking energizing.
      if (machinestate == RUNNING) {
        send(NULL, "event stop-pressed");
      } else if (machinestate >= POWERED) {
        send(NULL, "event powerdown");
        machinestate = WAITINGFORCARD;
      } else if (machinestate != WRONGFRONTSWITCHSETTING && machinestate > NOCONN) {
        send(NULL, "event frontswitchfail");
        machinestate = WRONGFRONTSWITCHSETTING;
      }
      break;
    case 1: // On/off switch in the proper setting, ok to energize.
      if (machinestate > POWERED) {
        send(NULL, "event stop-pressed");
        machinestate = WAITINGFORCARD;
      }
      if (machinestate == WRONGFRONTSWITCHSETTING) {
        send(NULL, "event frontswitchokagain");
        machinestate = WAITINGFORCARD;
      };
      break;
    case 3: // Relay energized, but not running.
      if (machinestate == POWERED) {
        send(NULL, "event start-pressed");
        machinestate = ENERGIZED;
      };
      if (machinestate == RUNNING) {
        send(NULL, "event halted");
        machinestate = ENERGIZED;
      } else if (machinestate < ENERGIZED && machinestate > NOCONN) {
        static int last_spur_detect = 0;
        if (millis() - last_spur_detect > 500) {
          send(NULL, "event spurious buttonpress?");
        };
        last_spur_detect = millis();
      };
      break;
    case 2: // Relay engergized and running.
      if (machinestate != RUNNING && machinestate > WAITINGFORCARD) {
        send(NULL, "event running");
        machinestate = RUNNING;
      }
      break;
  };

  int relayenergized = 0;
  switch (machinestate) {
    // XXX we git a slight challenge here - if above hardware state is funny - then
    // we may get into a situation where we skip BOOTING and NOTOFU.
    //
    case BOOTING:
      if (setup_curve25519() == 0) {
        machinestate = NOTOFU;
      }
      break;
    case NOTOFU:
      if (eeprom.flags & CRYPTO_HAS_MASTER_TOFU)
        machinestate = WAITINGFORCARD;
      break;
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
#ifdef PIN_AUTOOFF
      if (millis() - laststatechange > AUTOOFF_TO) {
        send(NULL, "event autoff");
        Log.println("Machine not used for too long; auto off.");
        machinestate = AUTOOFF;
      }
#endif
      break;
    case CHECKING:
      setRedLED(LED_FAST);
      if (millis() - laststatechange > CHECK_TO) {
        setRedLED(LED_OFF);
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
        send(NULL, "event energized");
      };
      if (millis() - laststatechange > IDLE_TO) {
        send(NULL, "event toolongidle");
        Log.println("Machine not used for too long; revoking access.");
        machinestate = WAITINGFORCARD;
      }
      relayenergized = 1;
      break;
    case RUNNING:
      setRedLED(LED_ON);
      relayenergized = 1;
      break;
    case AUTOOFF:
#ifdef PIN_AUTOOFF
      if (millis() - laststatechange > 1000) {
        digitalWrite(PIN_POWER, 0);
        pinMode(PIN_AUTOOFF, OUTPUT);
        digitalWrite(PIN_AUTOOFF, 1);
      }
#endif
      break;
  };

  digitalWrite(PIN_POWER, relayenergized);

  if (laststate != machinestate) {
#if DEBUG3
    Serial.printf("State: <%s> to <%s> 1=%d 2=%d red=%d P=%d\n", machinestateName[laststate], machinestateName[machinestate], v1, v2 red, relayenergized);
#endif
    laststate = machinestate;
    laststatechange = millis();
  }
}

int checkTagReader() {
#ifdef FAKEMODE
  static unsigned long last = 0;
  if (millis() - last < 10 * 1000)
    return 0;

  last = millis();

  MFRC522::Uid uid = { .size = 5, .uidByte = { 1, 1, 2, 3, 5 } };
#else
  if ( ! mfrc522.PICC_IsNewCardPresent())
    return 0;

  // Select one of the cards
  if ( ! mfrc522.PICC_ReadCardSerial())
    return 0;

  MFRC522::Uid uid = mfrc522.uid;
  if (uid.size == 0)
    return 0;
#endif

  lasttag[0] = 0;
  for (int i = 0; i < uid.size; i++) {
    char buff[5];
    snprintf(buff, sizeof(buff), "%s%d", i ? "-" : "", uid.uidByte[i]);
    strcat(lasttag, buff);
  }
  lasttagbeat = beatCounter;

  const char * tag_encoded;
  if (!eeprom.flags & CRYPTO_HAS_PRIVATE_KEYS) {
    char beatAsString[ MAX_BEAT ];
    snprintf(beatAsString, sizeof(beatAsString), BEATFORMAT, beatCounter);
    Sha256.initHmac((const uint8_t*)passwd, strlen(passwd));
    Sha256.print(beatAsString);
    Sha256.write(uid.uidByte, uid.size);
    tag_encoded = hmacToHex(Sha256.resultHmac());
  } else {
    CBC<AES256> cipher;
    uint8_t iv[16];
    RNG.rand(iv, sizeof(iv));

    if (!cipher.setKey(sessionkey, cipher.keySize())) {
      Log.println("FAIL setKey");
      return 0;
    }

    if (!cipher.setIV(iv, cipher.ivSize())) {
      Log.println("FAIL setIV");
      return 0;
    }

    // PKCS#7 padding - as traditionally used with AES.
    // https://www.ietf.org/rfc/rfc2315.txt 
    // -- section 10.3, page 21 Note 2.
    //
    size_t len = strlen(lasttag);
    int pad = 16 - (len % 16); // cipher.blockSize();
    if (pad == 0) pad = 16; //cipher.blockSize();

    size_t paddedlen = len + pad;
    uint8_t input[ paddedlen ], output[ paddedlen ], output_b64[ paddedlen * 4 / 3 + 4  ], iv_b64[ 32 ];
    strcpy((char *)input, lasttag);

    for (int i = 0; i < pad; i++)
      input[len + i] = pad;

    cipher.encrypt(output, (uint8_t *)input, paddedlen);
    encode_base64(iv, sizeof(iv), iv_b64);
    encode_base64(output, paddedlen, output_b64);

#if 0
    unsigned char key_b64[128];  encode_base64(sessionkey, sizeof(sessionkey), key_b64);
    Serial.print("Plain len="); Serial.println(strlen(lasttag));
    Serial.print("Paddd len="); Serial.println(paddedlen);
    Serial.print("Key Size="); Serial.println(cipher.keySize());
    Serial.print("IV Size="); Serial.println(cipher.ivSize());
    Serial.print("IV="); Serial.println((char *)iv_b64);
    Serial.print("Key="); Serial.println((char *)key_b64);
    Serial.print("Cypher="); Serial.println((char *)output_b64);
#endif

    char tmp[MAX_MSG];
    snprintf(tmp, sizeof(tmp), "%s.%s", iv_b64, output_b64);
    tag_encoded = tmp;
  }

  static char buff[MAX_MSG];
  snprintf(buff, sizeof(buff), "energize %s %s %s", moi, machine, tag_encoded);
  send(NULL, buff);

  return 1;
}

void loop() {
  maintain_rng();

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
  publish_loop();

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

    // Rekey if we're connected and more than 30 seconds out of touch - or had a beat skip.
    static unsigned long lst = 0;
    if (beatCounter - lst > 100) {
      Log.println("(Re)Announce after a long hiatus/beat reset.");
      send_helo("announce");
    }
    lst = beatCounter;
  };

#ifdef DEBUG3
  static unsigned long last_beat = 0;
  if (millis() - last_beat > 3000 && client.connected()) {
    send(NULL, "ping");
    last_beat = millis();
  }
#endif


  if (machinestate >= WAITINGFORCARD && millis() - laststatechange > 1500) {
    if (checkTagReader()) {
      laststatechange = millis();
      if (machinestate >= ENERGIZED)
        machinestate = WAITINGFORCARD;
      else
        machinestate = CHECKING;
    }
  }
}
