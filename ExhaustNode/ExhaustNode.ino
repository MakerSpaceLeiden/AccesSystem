
// Node MCU has a weird mapping...
//
#define LED_GREEN   16 // D0 -- LED inside the on/off toggle switch
#define LED_ORANGE  5  // D1 -- LED inside the orange, bottom, push button.
#define RELAY       4  // D2 -- relay (220V, 10A on just the L)
#define PUSHBUTTON  0  // D3 -- orange push button; 0=pressed, 1=released

// With the new OTA code -- All ESPs have
// enough room for the code -- though still need
// Over 328kB free to actually have enough room
// to be able to flash.
//
#define OTA

// Allow the unit to go into AP mode for reconfiguration
// if no wifi network is found. Note that this relies
// on a SPIFFS to store the config; so >= 2Mbyt flash is
// needed (and realistically 4Mbyte for the OTA).
//
#define CONFIGAP

// Comment out to reduce debugging output. Note that most key
// debugging is only visible on the serial port.
//
// #define DEBUG
// #define DEBUG_ALIVE  // sent an i-am-alive ping every 3 seconds.
//
#include <ESP8266WiFi.h>
#include <PubSubClient.h>        // https://github.com/knolleary/

#include <Ticker.h>
#include <SPI.h>

#include "MakerspaceMQTT.h"
#include "Log.h"

Log Log;

#define BUILD  __FILE__ " " __DATE__ " " __TIME__


#include "../../../../.passwd.h"

#ifndef CONFIGAP
const char ssid[34] = WIFI_NETWORK ;
const char wifi_password[34] = WIFI_PASSWD;
#endif


typedef enum {
  SWERROR, OUTOFORDER, NOCONN, // some error - machine disabLED.
  WAITINGFORCARD,             // waiting for card.
  POWERED,                    // Relay powered.
  // DUSTEXTRACT,
  NOTINUSE,
} machinestates_t;

const char *machinestateName[NOTINUSE] = {
  "Software Error", "Out of order", "No network",
  "Fan off",
  "Fan on",
  // "Fan runout",
};

static machinestates_t laststate;
unsigned long laststatechange = 0;
machinestates_t machinestate;
unsigned long beatCounter = 0;      // My own timestamp - manually kept due to SPI timing issues.

typedef enum { LED_OFF, LED_FLASH, LED_SLOW, LED_FAST, LED_ON, NEVERSET } LEDstate;

WiFiClient espClient;
PubSubClient client(espClient);

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
  debugFlash();
#endif

#ifdef CONFIGAP
  configBegin();

  static int debounce = 0;
  while (digitalRead(PUSHBUTTON) == 0 && debounce < 5) {
    debounce++;
    delay(5);
  };
  if (debounce >= 5)  {
    configPortal();
  }
  configLoad();
#endif

  WiFi.mode(WIFI_STA);
#ifdef CONFIGAP
  WiFi.SSID();
#else
  WiFi.begin(ssid, wifi_passwd);
#endif

  // Try up to 5 seconds to get a connection; and if that fails; reboot.
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start < 5000)) {
    delay(100);
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
  configureOTA();
  Log.print("IP address (and OTA enabled): ");
#else
  Log.print("IP address: ");
#endif
  Log.println(WiFi.localIP());

  SPI.begin();      // Init SPI bus

#ifdef HASRFID
  configureRFID(PIN_RFID_SS, PIN_RFID_RST);
#endif
  configureMQTT();

  machinestate = WAITINGFORCARD;
  setGreenLED(LED_ON);

#ifdef DEBUG
  debugListFS("/");
#endif
}


void handleMachineState() {

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
    case WAITINGFORCARD:
      if (machinestate <= NOCONN)
        setGreenLED(LED_ON);
      setOrangeLED(LED_FLASH);
      break;
    case POWERED:
      setOrangeLED(LED_ON);
      relayenergized = 1;
      break;
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


void loop() {
  // Keepting time is a bit messy; the millis() wrap around and
  // the SPI access to the reader seems to mess with the millis().
  //
  static unsigned long last_loop = 0;
  if (millis() - last_loop >= 1000) {
    unsigned long secs = (millis() - last_loop + 500) / 1000;
    beatCounter += secs;
    last_loop = millis();
  }

  handleMachineState();
  client.loop();
  mqttLoop();

#ifdef OTA
  otaLoop();
#endif

#ifdef DEBUG_ALIVE
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

#ifdef DEBUG
  // Emit the state of the node very 5 seconds or so.
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
}
