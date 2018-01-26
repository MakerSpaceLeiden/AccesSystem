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
#include "OTA.ino"
#endif
#include "SIG1.ino"
#include "SIG2.ino"
#include "Log.ino"
#include "MakerspaceMQTT.ino"

#include <SPI.h>
#include <MFRC522.h>

// Hardware random generator.
#include <esp8266-trng.h>

// For the HMAC of the SIG/1.0 protocol


#ifdef DEBUG
#include <GDBStub.h>
#endif

#include <EEPROM.h>

#include "../../../../.passwd.h"

const char* ssid = WIFI_NETWORK;
const char* wifi_password = WIFI_PASSWD;

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
void setup() {
  Serial.begin(115200);

  pinMode(PIN_POWER, OUTPUT);
  digitalWrite(PIN_POWER, 0);

  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, 1);

  pinMode(PIN_OPTO_ENERGIZED, INPUT);
  pinMode(PIN_OPTO_OPERATOR, INPUT);

  setRedLED(LED_FAST);

  Log.begin(mqtt_topic_prefix, 115200);
  Log.println("\n\n\nBuild: " BUILD);
#ifdef GDBSTUB_H
  Log.println("WARNING - serial output has been disabled as GDB is compiled in.");
#endif

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
  configureOTA();
#endif

  Log.print("IP address: ");
  Log.println(WiFi.localIP());

  SPI.begin();      // Init SPI bus
  mfrc522.PCD_Init();   // Init MFRC522
  mfrc522.PCD_DumpVersionToSerial();

  configureMQTT();
  init_curve()
  kickoff_RNG();

  machinestate = BOOTING;
  setRedLED(LED_ON);
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

  const char * tag_encoded[MAX_MSG];

  if (!eeprom.flags & CRYPTO_HAS_PRIVATE_KEYS) {
    char beatAsString[ MAX_BEAT ];
    snprintf(beatAsString, sizeof(beatAsString), BEATFORMAT, beatCounter);
    Sha256.initHmac((const uint8_t*)passwd, strlen(passwd));
    Sha256.print(beatAsString);
    Sha256.write(uid.uidByte, uid.size);
    tag_encoded = hmacToHex(Sha256.resultHmac());
  } else {
    if (!(tag_encoded = sig2_encrypt(lasttag, tag_encoded, sizeof(tag_encoded))))
      return;
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
  otaLoop();
#endif

  client.loop();
  publish_loop();
  mqttLoop();

  // Rekey if we're connected and more than 30 seconds out of touch - or had a beat skip.
  static unsigned long lst = 0;
  if (beatCounter - lst > 100) {
    Log.println("(Re)Announce after a long hiatus/beat reset.");
    send_helo("announce");
  }
  lst = beatCounter;

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
