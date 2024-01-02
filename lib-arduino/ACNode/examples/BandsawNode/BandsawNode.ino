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

   Compile settings:  EPS32 Dev Module
*/
#include <WhiteNodev108.h>

#ifndef MACHINE
#define MACHINE             "lintzaag"
#endif

#define INTERLOCK     (OPTO0) // Detect voltage on the interlock/safety contactor.
#define ONOFFSWITCH   (OPTO1) // Detects voltage on the normally-closed circuit of the front switch.
#define MOTOR_CURRENT (CURR0) // One of the 3-phase wires to the motor runs through this current coil.

#define RELAY_GPIO    (OUT0)  // The relay that sits in the safety interlock of 
                              // the contactor at the back-bottom of the saw.

// Generate with 'echo -n Password | openssl md5 or
// use https://www.md5hashgenerator.com/. No \0,
// cariage return or linefeed  at the end of the
// password; just the characters of the password
// itself.
//
// #define OTA_PASSWD_MD5  "0f475732f6c1a632b3e161160be0cfc5" // the MD5 of "SomethingSecrit"
//
#ifndef OTA_PASSWD_HASH
#error "An OTA password hash(md5) MUST be set. Sorry."
#endif

WhiteNodev108 node = WhiteNodev108(MACHINE);

unsigned long bad_poweroff = 0, normal_poweroff = 0;


ButtonDebounce *interlockDetect, *motorCurrent, *onoffSwitchDetect;


// Extra state above 'POWERED' - when the saw is spinning (detected via the motorCurrent) as
// opposed to the safety circuitry being powered (i.e. relay has closed, so the interlock
// circuit with the eStop allows the main contactor to be on.
//
MachineState::machinestate_t RUNNING;

static void tellOff(const char *msg) {
  node.updateDisplay(msg,"","");
  for (int i = 0; i < 9; i++) {
    node.buzzerErr();
    delay(300);
  };
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n\n");
  Serial.println("Booted: " __FILE__ " " __DATE__ " " __TIME__ );

  // Init the hardware and get it into a safe state.
  // Init the hardware and get it into a safe state.
  //
  pinMode(RELAY_GPIO, OUTPUT);
  digitalWrite(RELAY_GPIO, 0);

  // Define a state other than powered; i.e. where the saw is actually spinning.
  //
  RUNNING = node.machinestate.addState("Saw Running", LED::LED_ON, MachineState::NEVER, 0);

  pinMode(INTERLOCK, INPUT);
  interlockDetect = new ButtonDebounce(INTERLOCK);
  interlockDetect->setCallback([](const int newState) {
    if (node.machinestate == MachineState::CHECKINGCARD && newState == LOW) {
      Log.println("Alert: Powere on the interlock observed while " MACHINE " should be locked.");
      node.machinestate = FAULTED;
    }
    else if (node.machinestate == FAULTED && newState == HIGH) {
      Log.println("Alert: Odd powerstate cleared.");
      node.machinestate = MachineState::WAITINGFORCARD;
    }
    else if (node.machinestate == RUNNING && newState == HIGH) {
      Log.println("Alert: " MACHINE " was powered off by the relay while the motor was runing. Bad.");
      node.machinestate = MachineState::WAITINGFORCARD;
      tellOff("Always use the switch on the front to poweroff");
      bad_poweroff++;
    }
    else if (node.machinestate == POWERED && newState == HIGH) {
      Log.println("Normal poweroff with the green button.");
      node.machinestate = MachineState::WAITINGFORCARD;
      normal_poweroff++;
    }
    else
      Debug.printf("Interlock power now %s\n", newState ? "OFF" : "ON");
  }, CHANGE);

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
      Log.printf("Alert: Unexpected change in motor current; state is %s and the current is %s\n",
                 node.machinestate.label(), newState ? "ON" : "OFF");
    }
  }, CHANGE);

  node.setOTAPasswordHash(OTA_PASSWD_HASH);
  node.set_mqtt_prefix("ac");
  node.set_master("master");

  node.onReport([](JsonObject & report) {
    report["bad_poweroff"] = bad_poweroff;
    report["normal_poweroff"] = normal_poweroff;
    report["fw"] = __FILE__ " " __DATE__ " " __TIME__;
  });

  node.begin();

  node.setOnChangeCallback(MachineState::ALL_STATES, [](MachineState::machinestate_t last, MachineState::machinestate_t current) -> void {
    if (current == RUNNING || current == POWERED) {
      // We do not show the 'OFF' button - we expect the user to use
      // the RED/Green on/off button of the safety contactor.
      node.updateDisplay("", "", true);
    };
  });

  node.onApproval([](const char *machine) {
    Log.println("Approval callback");
    // We allow 'taking over this achine while it is on' -- hence this check for
    // if it is powered; and in that case -also- accepting a new approval.
    //
    if (node.machinestate != POWERED & node.machinestate != MachineState::CHECKINGCARD) {
      node.buzzerErr();
      return;
    };
    node.machinestate = POWERED;
  });

  Log.println("Starting loop(): " __FILE__ " " __DATE__ " " __TIME__);
}

void loop() {
  node.loop();

  digitalWrite(RELAY_GPIO,
               ((node.machinestate == POWERED) || (node.machinestate == RUNNING)) ? HIGH : LOW);
}
