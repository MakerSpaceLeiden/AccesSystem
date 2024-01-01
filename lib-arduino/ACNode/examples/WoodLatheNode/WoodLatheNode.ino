/*
      Copyright 2015-2018 Dirk-Willem van Gulik <dirkx@webweaving.org>
                          Stichting Makerspace Leiden, the Netherlands.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, softwareM
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF
   ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

   Compile settings:  EPS32 Dev Module (if it makes noise - you likely picked
   the wrong board- and a LED signal on GPIO2 drives the buzzer by accident).

   Max reliable serial speed: 460800
   Compile settings:  EPS32 Dev Module

*/
#include <WhiteNodev108.h>

#ifndef MACHINE
#define MACHINE   "woodlathe"
#endif

#define RELAY_GPIO        (OUT0)
#define MOTOR_CURRENT     (CURR0)

//#define OTA_PASSWD_MD5  "0f475732f6c1a632b3e161160be0cfc5" // the MD5 of "SomethingSecrit"

#ifndef OTA_PASSWD_HASH
#error "An OTA password MUST be set as a MD5. Sorry."
// Generate with 'echo -n Password | openssl md5 or
// use https://www.md5hashgenerator.com/. No \0,
// cariage return or linefeed  at the end of the
// password; just the characters of the password
// itself.
#endif

WhiteNodev108 node = WhiteNodev108(MACHINE);

ButtonDebounce * motorCurrent;
MachineState::machinestate_t RUNNING;

unsigned long bad_poweroff = 0, normal_poweroff = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n\n");
  Serial.println("Booted: " __FILE__ " " __DATE__ " " __TIME__ );

  // Init the hardware and get it into a safe state.
  //
  digitalWrite(RELAY_GPIO, 0);
  pinMode(RELAY_GPIO, OUTPUT);

  // Define a state other than powered; i.e. where the saw is actually spinning.
  //
  RUNNING = node.machinestate.addState("Saw Running", LED::LED_ON, MachineState::NEVER, 0);

  motorCurrent = new ButtonDebounce(MOTOR_CURRENT);
  motorCurrent->setAnalogThreshold(600);  // typical is 0-50 for off, 1200 for on.
  motorCurrent->setCallback([](const int newState) {
    if (node.machinestate == POWERED && newState) {
      Debug.println("Detected current. Motor switched on");
      node.machinestate = RUNNING;
    } else if (node.machinestate == RUNNING && !newState) {
      Debug.println("No more current; motor no longer on.");
      node.machinestate = POWERED;
    } else {
      Log.printf("Unexpected change in motor current; state is %s and the current is %s\n",
                 node.machinestate.label(), newState ? "ON" : "OFF");
    }
  }, CHANGE);

  node.setOTAPasswordHash(OTA_PASSWD_HASH);
  node.set_mqtt_prefix("ac");
  node.set_master("master");

  node.onReport([](JsonObject & report) {
    report["normal_poweroff"] = normal_poweroff;
    report["fw"] = __FILE__ " " __DATE__ " " __TIME__;
  });

  node.begin();

  node.onApproval([](const char *machine) {
    Log.println("Approval callback");
    // We allow 'taking over a machine while it is on' -- hence this check for
    // if it is powered.
    if (node.machinestate != POWERED & node.machinestate != MachineState::CHECKINGCARD) {
      node.buzzerErr();
      return;
    };
    node.machinestate = POWERED;
  });

  // This node does not have a safety contactor; instead it has a single (off) button
  // that also contains the (error/aart) general indicator LED.
  //
  node.setOffCallback([](const int newState) {
    if (node.machinestate == POWERED && newState == LOW) {
      Log.println("Normal poweroff");
      normal_poweroff++;
      node.machinestate = MachineState::CHECKINGCARD;
      node.buzzerOk();
    }
    else if (node.machinestate == RUNNING && newState == LOW) {
      // Refuse to let the safety be used to power something off. As
      // the relay is really not designed for this.
      //
      Log.println("Bad poweroff attempt. Ignoring.");
      node.buzzerErr();
      bad_poweroff++;
    } else
      Debug.println("Left button press ignored.");
  },
  WHEN_PRESSED);

  Log.println("Booted: " __FILE__ " " __DATE__ " " __TIME__);
}

void loop() {
  node.loop();
  digitalWrite(RELAY_GPIO,
               ((node.machinestate == POWERED) || (node.machinestate == RUNNING)) ? HIGH : LOW);
}
