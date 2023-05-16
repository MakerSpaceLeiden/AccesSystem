/*
 *    Copyright 2015-2016 Dirk-Willem van Gulik <dirkx@webweaving.org>
 *                        Stichting Makerspace Leiden, the Netherlands.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LED                0  // LED on the front; in the push/toggle button
#define RELAY           15  // 5A relay that controls the output

#define HALL            A0  // HALL sensor, ACS712 -- analog output with resitor bridge.
#define HALL_RANGE       5  // Range of HALL sensor; 5 Ampere.

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
#include <WiFiUdp.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <PubSubClient.h>        // https://github.com/knolleary/
#include <SPI.h>
#include <FS.h>
#include <SPI.h>
#include <FS.h>

// ArduinoJSON library -- from https://github.com/bblanchon/ArduinoJson
//
// Depending on your version - if you get an osbcure error in
// .../ArduinoJson/Polyfills/isNaN.hpp and isInfinity.hpp - then
// isnan()/isinf() to __builtin_isnXXX() around line 34-36/
//
#include <ArduinoJson.h>

#include "MakerspaceMQTT.h"
#include "OTA.h"
#include "RFID.h"
#include "Log.h"
#include "LEDs.h"
#include "Signaling.h"
#include "ConfigPortal.h"


class Node : public ConfigPortal, public OTA, public RFID;

WiFiClient espClient;
PubSubClient client(espClient);
Log Log;
Node node;

// ACNode acnode;

#define BUILD  __FILE__ " " __DATE__ " " __TIME__

#include "../../../../.passwd.h"

#ifndef CONFIGAP

// Hardcoded, compile time settings.
//
const char ssid[34] = WIFI_NETWORK ;
const char wifi_password[34] = WIFI_PASSWD;
#endif


typedef enum {
  SWERROR, OUTOFORDER, NOCONN, // some error - machine disabLED.
  WAITINGFORCARD,             // waiting for card.
  POWERED,                    // Relay powered.
  NOTINUSE,
} machinestates_t;

const char *machinestateName[NOTINUSE] = {
  "Software Error", "Out of order", "No network",
  "Waiting for cardf",
  "Powered on",
};

static machinestates_t laststate;
unsigned long laststatechange = 0;
machinestates_t machinestate;
unsigned long beatCounter = 0;      // My own timestamp - manually kept due to SPI timing issues.

LED led;

void signalStateToUser(signalState_t newState) {
  switch (newState) {
    case STATE_BOOTING:            // pre-interaction not ready state (as oposed to STATE_NORMAL_PLEASE_WAIT).
      led = LED_FAST;
      break;
    case STATE_CONFIG:             // in configuration state (e.g. OTA) - waiting for the user to interact with it.
      led = LED_SLOW;
      break;
    case STATE_UPDATE:             // being updated - user should not do anything.
      led = LED_FAST;
      break;
    case STATE_NORMAL_READY:       // device can be interacted with it
      led = LED_ON_SLOW;
      break;
    case STATE_NORMAL_ACTIVATED:   // device can be interacted with - it is 'on' or doing its thing.
      led = LED_ON;
      break;
    case STATE_NORMAL_PLEASE_WAIT: // device is doing something - cannot be interacted with.
      led = LED_FAST;
      break;
    case STATE_OFF:                // not currently used.
    case STATE_ERROR:             // error; device is inoperable; user cannot do anything - device is trying to sort itself out.
    case STATE_ERROR_HANG:         // error; device is inoperable; user cannot do anything (but powercycle, etc).
    default:
      led = LED_FAST;
      break;
  }
}

void setup() {
  digitalWrite(RELAY, 0); // Stop the relay from fluttering during pinMode() change.
  pinMode(RELAY, OUTPUT);

  led.begin("red", LED);
  signalStateToUser(STATE_BOOTING);

  SPI.begin(); // Init SPI bus

  Log.begin(node.mqtt_topic_prefix, node.logpath, 115200);
  Log.println("\n\n\nBuild: " BUILD);

#ifdef DEBUG
  debugFlash();
#endif

  node = Node();

#ifdef CONFIGAP
#if 0
  static int debounce = 0;
  while (digitalRead(PUSHBUTTON) == 0 && debounce < 5) {
    debounce++;
    delay(5);
  };
  if (debounce >= 5)  
    runConfigPortal();
#endif
  configLoad();
#endif

  WiFi.mode(WIFI_STA);
#ifdef CONFIGAP
  WiFi.SSID();
#else
  WiFi.begin(ssid, wifi_passwd);
#endif

  // Try up to 5 seconds to get a WiFi connection; and if that fails; reboot
  // with a bit of a delay.
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start < 5000)) {
    delay(100);
  };

  if (WiFi.status() != WL_CONNECTED) {
    Log.printf("Connection to <%s>\n", WiFi.SSID().c_str());
    orangeLED.setState(LED_FAST);
    Log.println("Rebooting...");
    delay(5000);
    ESP.restart();
  }

  Log.printf("Wifi connected to <%s>\n", WiFi.SSID().c_str());

#ifdef OTA
  // Only allow OTA post (any) Wifi portal config -- as otherwise the
  // latter can timeout in the middle of an OTA update without ado.
  //
  configureOTA(nodename, (const char *)OTA_PASSWD);
#endif

  Log.print("IP address: ");
  Log.println(WiFi.localIP());

#ifdef HASRFID
  configureRFID(PIN_RFID_SS, PIN_RFID_RST);
#endif
  configureMQTT();

  machinestate = WAITINGFORCARD;
  greenLED.setState(LED_ON);

#ifdef DEBUG
  debugListFS("/");
#endif
}

void machineLoop() {

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

  // Go green if we just left some error state.
  //
  if (laststate <= NOCONN && machinestate > NOCONN)
    led.setState(LED_ON);

  int relayenergized = 0;
  switch (machinestate) {
    case OUTOFORDER:
    case SWERROR:
    case NOCONN:
    case NOTINUSE:
      led.setState(LED_FAST);
      break;
    case WAITINGFORCARD:
      if (machinestate <= NOCONN)
        led = LED_FLASH;
      else
        led = LED_SLOW;
      break;
    case POWERED:
      led = LED_ON;
      relayenergized = 1;
      break;
  };

  digitalWrite(RELAY, relayenergized);

  if (laststate != machinestate) {
    laststate = machinestate;
    laststatechange = millis();
  }
}


void loop() {
  // Keepting time is a bit messy; the millis() wrap around and
  // the SPI access to the reader seems to mess with the millis().
  // So we revert to doing 'our own'.
  //
  static unsigned long last_loop = 0;
  if (millis() - last_loop >= 1000) {
    unsigned long secs = (millis() - last_loop + 499) / 1000;
    beatCounter += secs;
    last_loop = millis();
  }

  machineLoop();
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
