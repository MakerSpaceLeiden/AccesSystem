/*
      Copyright 2015-2016 Dirk-Willem van Gulik <dirkx@webweaving.org>
                          Stichting Makerspace Leiden, the Netherlands.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/
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

// If it has wired ethernet -- this will disable WiFi.
// Actual pinning defined in WiredEthernet.ino.
//
#define WIRED_ETHERNET

// Allow the unit to go into AP mode for reconfiguration
// if no wifi network is found. Note that this relies
// on a SPIFFS to store the config; so >= 2Mbyt flash is
// needed (and realistically 4Mbyte for the OTA).
//
#define CONFIGAP

// Comment out to reduce debugging output. Note that most key
// debugging is only visible on the serial port.
//
#define DEBUG

// sent an i-am-alive ping every 3 seconds.
#define DEBUG_ALIVE

#ifdef  ESP32
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#else
#include <ESP8266WiFi.h>
#endif
#include <PubSubClient.h>        // https://github.com/knolleary/

#include <SPI.h>

#include "MakerSpaceMQTT.h"
#include "Log.h"
#include "LEDs.h"
#include "OTA.h"
#include "ConfigPortal.h"
#include "RFID.h"

WiFiClient espClient;
PubSubClient client(espClient);
Log Log;

#define BUILD  __FILE__ " " __DATE__ " " __TIME__ " " ARDUINO_BOARD

#ifndef CONFIGAP
// Hardcoded, compile time settings.
const char ssid[34] = WIFI_NETWORK ;
const char wifi_password[34] = WIFI_PASSWD;
#endif


typedef enum {
  SWERROR, OUTOFORDER, NOCONN, // some error - machine disabLED.
  WAITINGFORCARD,             // waiting for card.
  POWERED,                    // Relay powered.
  // DUSTEXTRACT,
  PAUSED_AFTER_ERROR,
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

void setup() {
  digitalWrite(RELAY, 0); // Stop the relay from fluttering during pinMode() change.
  pinMode(RELAY, OUTPUT);

  {// Start with the exhaust fan ON, so you don't have to wait for WiFi to turn the fan on
  machinestate = POWERED;
  digitalWrite(RELAY, 1);
  }

  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_ORANGE, OUTPUT);
  pinMode(PUSHBUTTON, INPUT);

  setGreenLED(LED_FAST);
  setOrangeLED(LED_OFF);

  SPI.begin(); // Init SPI bus

  Log.begin(mqtt_topic_prefix, 115200);
  Log.println("\n\n\nBuild: " BUILD
#ifdef OTA
              " ota"
#endif
#ifdef CONFIGAP
              " configAP"
#endif
#ifdef WIRED_ETHERNET
              " ethernet"
#else
              " wifi"
#endif
#ifdef DEBUG
              " debug"
#endif
#ifdef DEBUG_ALIVE
              " fast-alive-beat"
#endif
#ifdef HASRFID
              " rfid-reader"
#endif
#ifdef SIG1
              " sig1"
#endif
#ifdef SIG2
              " sig2"
#endif
             );

#ifdef DEBUG
  debugFlash();
#endif

#ifdef WIRED_ETHERNET
#ifdef  ESP32
  Debug.println("starting up ethernet");
  eth_setup();
#endif
#endif

#ifdef CONFIGAP
  configBegin();

  // Go into Config AP mode if the orange button is pressed
  // just post powerup -- or if we have an issue loading the
  // config.
  //
  static int debounce = 0;
  while (digitalRead(PUSHBUTTON) == 0 && debounce < 5) {
    debounce++;
    delay(5);
  };
  if (debounce >= 5 || configLoad() == 0)  {
    configPortal();
  }
#endif

#ifndef WIRED_ETHERNET
  Debug.println("starting up wifi");
  WiFi.mode(WIFI_STA);
#endif

#ifdef CONFIGAP
  WiFiManager wifiManager;
  wifiManager.autoConnect();
#else
  WiFi.begin(ssid, wifi_passwd);
#endif

  const int del = 10; // seconds.

  // Try up to del seconds to get a WiFi connection; and if that fails; reboot
  // with a bit of a delay.
  //
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start < del * 1000)) {
    delay(100);
  };

#ifndef WIRED_ETHERNET
  if (WiFi.status() != WL_CONNECTED) {
    Log.printf("No connection after %d seconds (ssid=%s). Going into config portal (debug mode);.\n", del, WiFi.SSID().c_str());
    configPortal();
#if 0
    Log.printf("No connection after %d seconds (ssid=%s). Rebooting.\n", del, WiFi.SSID().c_str());
    setOrangeLED(LED_FAST);
    Log.println("Rebooting...");
    delay(1000);
    ESP.restart();
#endif
  }
  Log.printf("Wifi connected to <%s>\n", WiFi.SSID().c_str());
#endif

#ifdef OTA
  // Only allow OTA post (any) Wifi portal config -- as otherwise the
  // latter can timeout in the middle of an OTA update without ado.
  //

  configureOTA();
#endif

  Log.print("IP address: ");
  Log.println(WiFi.localIP());

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
      send(NULL, "event manual-start");
    }
    else if (machinestate >= POWERED) {
      machinestate = WAITINGFORCARD;
      send(NULL, "event manual-stop");
    } else
      send(NULL, "event spurious-button-press");
  }
  if (digitalRead(PUSHBUTTON) == 1 && millis() - last_button_detect > 400) {
    last_button_detect =  0;
  }

  // Go green if we just left some error state.
  //
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
    case PAUSED_AFTER_ERROR:
      if (millis() - laststatechange > 1000) {
        machinestate = WAITINGFORCARD;
        setRedLED(LED_OFF);
      }
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
  otaLoop();
#endif


  machineLoop();
  mqttLoop();

#ifdef DEBUG_ALIVE
  static unsigned long last_beat = 0;
  if (millis() - last_beat > 3000 && client.connected()) {
    send(NULL, "ping");
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
    Log.print(" Wifi= ");
    Log.print(WiFi.status());
    Log.print(WiFi.status() == WL_CONNECTED ? "(connected)" : "");
    Log.print(" MQTT=<");
    Log.print(state2str(client.state()));
    Log.print(">");

    Log.print(" Button="); Log.print(digitalRead(PUSHBUTTON)  ? "not-pressed" : "PRESSed");
    Log.print(" Relay="); Log.print(digitalRead(RELAY)  ? "ON" : "off"); // 
    Log.println(".");

    last_debug = millis();
    last_debug_state = machinestate;
  }
#endif
}
