/*
      Copyright 2015-2018 Dirk-Willem van Gulik <dirkx@webweaving.org>
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

  Board: v1.14 / blue; with solenoid on OUT0 and microswitch on OPTO0

  Compile settings:
  - ESP32-WROOM-DA Module (or ESP32 Dev) with minial SPIFFs
  Wiring:
  - https://wiki.makerspaceleiden.nl/mediawiki/index.php/Node_Engraver

  History:  Aart v2 node until end of 2024 (with the power from a
  separate PoE converter); updated by v1.14 blue node.

*/
#include <BlueNodev114.h>

#define MACHINE          "engraver"

#define SOLENOID_GPIO (node.OUT0)   // Top MOSFET; wired to switch 24volt
#define SENSE_PIN     (node.OPTO0)  // Low when locked, High when open

#define BUZZ_TIME     (50) // How long to engage, in milli seconds
#define GRAB_TIME     (5000) // How long to engage, in milli seconds

#define LED_GREEN   (node.LEDD)
#define LED_RED     (node.LEDC)

// Generate with 'echo -n Password | openssl md5 or
// use https://www.md5hashgenerator.com/. No \0,
// cariage return or linefeed  at the end of the
// password; just the characters of the password
// itself.
//
// E.g.
//      /bin/echo -n "SomethingSecrit" | openssl md5
// to yeild below:
// #define OTA_PASSWD_HASH  "0f475732f6c1a632b3e161160be0cfc5"
//
#ifndef OTA_PASSWD_HASH
#error "An OTA password hash(md5) MUST be set. Sorry."
#endif
const char ota_password_hash[] = OTA_PASSWD_HASH;

BlueNodev114 node = BlueNodev114(MACHINE);

MachineState::machinestate_t BUZZING; 
MachineState::machinestate_t INUSE;
MachineState::machinestate_t STOLEN;
MachineState::machinestate_t GRABBALE;

unsigned long opening_count  = 0, denied_count = 0, lost_count = 0;

void setup() {
  Serial.println("setup(): " __FILE__ " " __DATE__ " " __TIME__ );

  expandedPinMode(SOLENOID_GPIO, OUTPUT);
  expandedDigitalWrite(SOLENOID_GPIO, LOW);

  // Add the states needed for this node. The mechnics are a bit messy; we have
  // to pulse the solenoid quickly; but it takes a bit longert to see if the key
  // really was released. So we buzz for 50 milli second; and then have a second
  // 'on' period; grabbable, during which we see if the key was taken. And if not
  // we fall back to waiting for a card.
  //
  GRABBALE = node.machinestate.addState((const char*)"Grab key",
                                       LED::LED_IDLE,
                                       (time_t)(GRAB_TIME), // how fast to grab the key
                                       node.machinestate.WAITINGFORCARD // then go back to waiting for the next swipe.
                                      );
  BUZZING = node.machinestate.addState((const char*)"Approved",
                                       LED::LED_IDLE,
                                       (time_t)(BUZZ_TIME), // stay in this state for BUZZ_TIME milli Seconds
                                       GRABBALE // then wait to see if it was removed.
                                      );
  INUSE = node.machinestate.addState((const char*)"Key in USE",
                                     LED::LED_IDLE,
                                     (time_t)(0), // stay in this state for-ever
                                     node.machinestate.WAITINGFORCARD // then go back to waiting for the next swipe.
                                    );
  STOLEN = node.machinestate.addState((const char*)"Key LOST",
                                      LED::LED_ERROR,
                                      (time_t)(0), // stay in this state for-ever
                                      node.machinestate.WAITINGFORCARD // then go back to waiting for the next swipe.
                                     );

  node.onApproval([](const char *machine) {
    node.machinestate = BUZZING;
    opening_count++;
  });

  node.onDenied([](const char *machine) {
    node.buzzerErr();
    denied_count++;
  });

  node.setOTAPasswordHash(ota_password_hash);
  node.set_mqtt_prefix("ac");
  node.set_master("master");

  node.onReport([](JsonObject & report) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s %s %s", FILE2FIRMWARE(__FILE__), __DATE__, __TIME__);
    report["fw"] = tmp;
    report["count_open"] = opening_count;
    report["count_denied"] = denied_count;
    report["count_lost"] = lost_count;
  });

  node.begin();
  const char * p = __FILE__;
  const char * q = rindex(p, '/');
  Log.printf("Fully booted: %s " __DATE__ " " __TIME__, q ? q + 1 : p);
}

void loop() {
  node.loop();

  // handle the open functon 'always'. Which boils down to
  // turing the MOSFET that controils the relay of the solenoid
  // of the lock on when we are in buzzing mode. Buzzing mode has a
  // timeout of BUZZ_TIME - after which we return back to WAITINGFORCARD.
  //
  expandedDigitalWrite(SOLENOID_GPIO, (node.machinestate.state() == BUZZING));

  // And also buzz during this time
  //
  node.buzzer((node.machinestate.state() == BUZZING || node.machinestate.state() == GRABBALE));

  // Have LEDs reflect the lock state
  //
  expandedAnalogWrite(LED_GREEN, (expandedDigitalRead(SENSE_PIN) == LOW) ? 150 : 0); // Green if lock is in the locked position
  expandedAnalogWrite(LED_RED, (expandedDigitalRead(SENSE_PIN) == HIGH) ? 255 : 0);  // Red if the lock/door is opened

  if ((node.machinestate.state() == BUZZING || node.machinestate.state() == GRABBALE) && expandedDigitalRead(SENSE_PIN) == HIGH) {
    Log.println("Key removed after unlock with a valid tag");
    node.machinestate = INUSE;
  } else if (node.machinestate.state() >= INUSE && expandedDigitalRead(SENSE_PIN) == LOW) {
    Log.println("Key is back");
    node.machinestate = node.machinestate.WAITINGFORCARD;
  } else if (node.machinestate.state() < GRABBALE && expandedDigitalRead(SENSE_PIN) == HIGH) {
    Log.println("Naughty - key was removed by prying");
    node.machinestate = STOLEN;
    lost_count++;
  };
}
