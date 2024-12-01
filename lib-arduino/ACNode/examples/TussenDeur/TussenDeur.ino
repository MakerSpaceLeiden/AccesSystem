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

  Board: v1.11 / black; with no screen and a solenoid on OUT0

  Compile settings:
  - ESP32-WROOM-DA Module (or ESP32 Dev) with minial SPIFFs
  Wiring:
  - https://wiki.makerspaceleiden.nl/mediawiki/index.php/Node_Tussendeur

  History:  Aart v2 node until end of 2024 (with the power from a
  separate PoE converter); updated by v1.12 black node.

*/
#include <BlackNodev111.h>

#define MACHINE          "tussendeur"

#define SOLENOID_GPIO (node.OUT1) // Bottom relay; wired to switch 12v
#define BUZZ_TIME     (4) // How long to busezz the door open.

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

BlackNodev111 node = BlackNodev111(MACHINE);

MachineState::machinestate_t BUZZING; // Extra, hardware specific states

unsigned long opening_door_count  = 0, door_denied_count = 0;

void setup() {
  Serial.println("setup(): " __FILE__ " " __DATE__ " " __TIME__ );

  digitalWrite(SOLENOID_GPIO, LOW);
  pinMode(SOLENOID_GPIO, OUTPUT);
  digitalWrite(SOLENOID_GPIO, LOW);

  // Add the states needed for this node.
  //
  BUZZING = node.machinestate.addState((const char*)"Approved",
                                       LED::LED_IDLE,
                                       (time_t)(BUZZ_TIME * 1000), // stay in this state for BUZZ_TIME seconds
                                       node.machinestate.WAITINGFORCARD // then go back to waiting for the next swipe.
                                      );

  node.onApproval([](const char *machine) {
    Log.printf("Engaging the buzzer\n");
    node.machinestate = BUZZING;
    opening_door_count++;
  });
  node.onDenied([](const char *machine) {
    node.buzzerErr();
    door_denied_count++;
  });

  node.setOTAPasswordHash(ota_password_hash);
  node.set_mqtt_prefix("ac");
  node.set_master("master");

  node.onReport([](JsonObject & report) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s %s %s", FILE2FIRMWARE(__FILE__), __DATE__, __TIME__);
    report["fw"] = tmp;
    report["count_open"] = opening_door_count;
    report["count_denied"] = door_denied_count;
  });

  node.begin();
  Log.println("Booted: " __FILE__ " " __DATE__ " " __TIME__ );
}

void loop() {

  // handle the open functon 'always'. Which boils down to
  // turing the MOSFET that controils the relay of the solenoid
  // of the lock on when we are in buzzing mode. Buzzing mode has a
  // timeout of BUZZ_TIME - after which we return back to WAITINGFORCARD.
  //
  digitalWrite(SOLENOID_GPIO, (node.machinestate.state() == BUZZING));
  //
  // And also buzz during this time
  //
  node.buzzer((node.machinestate.state() == BUZZING));

  node.loop();
}
