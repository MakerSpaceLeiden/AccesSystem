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

      Compile settings:  EPS32 Dev Module
      Serial: 460.8kBaud

   Note - if the buzzer 'screams' during boot - you propably selected an
          ESP32 devboard with a LED that flashes during boot/network traffic.
          Which is wired to the same wire as the buzzer.

*/
#include <WhiteNodev108.h>

#ifndef MACHINE
#define MACHINE "ceramicsnode"
#endif

// Generate with 'echo -n Password | openssl md5 or
// use https://www.md5hashgenerator.com/. No \0,
// cariage return or linefeed  at the end of the
// password; just the characters of the password
// itself.
//
//#define OTA_PASSWD_MD5  "0f475732f6c1a632b3e161160be0cfc5" // the MD5 of "SomethingSecrit"

#ifndef OTA_PASSWD_HASH
#error "An OTA password MUST be set as a MD5. Sorry."
#endif

#define RELAY_NO_START  (OUT0)      // output -- NO is the 'START' signal for the safety contactor
#define RELAY_NC_STOP   (OUT1)      // output -- NC is part of the interlock/safety circuit

#define OVEN_CURRENT    (CURR0)      // Current of one of the 3 phases running to the oven control relay
#define WHPULS_GPIO     (CURR1)      // Wh pulse, hard 4k7 pullup to 3v3; open Collector output of kWh meter

#define SAFETY          (OPTO0)     // Wired to the output of the interlock controlled relay/overcurrent switch
#define VK2000_GPIO     (OPTO1)     // Wired to the second relay on the VK2000 - closes when heater is switched on by VK2000

// Length to 'press' the safety buttons to swith the main safety
// contactor on or off. Should not be too short; to allow for the
// damping of the induction pulse.
//
#define SAFETY_RELAY_BUTTON_PRESS_LENGTH (300 /* mSeconds */)

// This node is currently not wired; instead it uses an AC/DC convertor
// and is wired into the same 3P+N+E power as the oven itself.
//
WhiteNodev108 node = WhiteNodev108(MACHINE, WIFI_NETWORK, WIFI_PASSWD);

// Extra state above 'POWERED' - when the pottery oven is in use as
// opposed to the safety circuitry being powered.
//
MachineState::machinestate_t FIRING;

ButtonDebounce *safetyDetect, *ovenCurrent, * vk2000detect;

unsigned long startWhCounter = 0;
volatile unsigned long whCounter = 0;
void IRAM_ATTR irqWattHourPulse() {
  whCounter++;
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n\n");
  Serial.println("Booted: " __FILE__ " " __DATE__ " " __TIME__);

  // Init the hardware and get it into a safe state.
  //
  digitalWrite(RELAY_NO_START, LOW);
  pinMode(RELAY_NO_START, OUTPUT);
  digitalWrite(RELAY_NO_START, LOW);

  digitalWrite(RELAY_NC_STOP, LOW);
  pinMode(RELAY_NC_STOP, OUTPUT);
  digitalWrite(RELAY_NC_STOP, LOW);

  FIRING = node.machinestate.addState("Firing", LED::LED_ON, 60 * 60 * 1000, POWERED);

  pinMode(VK2000_GPIO, INPUT);
  vk2000detect = new ButtonDebounce(VK2000_GPIO);

  vk2000detect->setCallback([](const int newState) {
    Log.printf("VK2000 output now %s\n", newState ? "OFF" : "ON");
  },
  CHANGE);

  pinMode(SAFETY, INPUT);
  safetyDetect = new ButtonDebounce(SAFETY);
  safetyDetect->setCallback([](const int newState) {
    Log.printf("Interlock power now %s\n", newState ? "OFF" : "ON");
    if (node.machinestate == MachineState::CHECKINGCARD && !newState) {
      Log.printf("Machine turned on by tag swipe.\n");
    }
    else if (node.machinestate == POWERED && newState) {
      Log.printf("Machine turned off\n");
    };

    // We are not policing the state here yet
    node.machinestate = newState ?  POWERED : MachineState::WAITINGFORCARD;
  },
  CHANGE);

  pinMode(WHPULS_GPIO, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(WHPULS_GPIO), irqWattHourPulse, FALLING);

  ovenCurrent = new ButtonDebounce(OVEN_CURRENT);
  ovenCurrent->setAnalogThreshold(600);
  ovenCurrent->setCallback([](const int newState) {
    Log.printf("Current to heating coil now %s\n", newState ? "OFF" : "ON");
  },
  CHANGE);

  node.setOffCallback([](const int newState) {
    if (node.machinestate == POWERED) {
      Log.println("Switching off power");

      // Break the safety interlock to power everything off. The
      // equivalent of pressing the red stop button.
      //
      digitalWrite(RELAY_NC_STOP, HIGH);
      delay(SAFETY_RELAY_BUTTON_PRESS_LENGTH);
      digitalWrite(RELAY_NC_STOP, LOW);

      // We rely on the change-stage callback to see the
      // power drop - and then change the state there.
      return;
    };
    Debug.printf("Left button %s ignored.", newState ? "release" : "press");
  },
  WHEN_PRESSED);

  node.setOTAPasswordHash(OTA_PASSWD_HASH);
  node.set_mqtt_prefix("ac");
  node.set_master("master");

  node.onReport([](JsonObject & report) {
    report["WhCounter"] = whCounter;
    report["fw"] = __FILE__ " " __DATE__ " " __TIME__;
  });

  node.begin();
  node.setOnChangeCallback(MachineState::ALL_STATES, [](MachineState::machinestate_t last, MachineState::machinestate_t current) -> void {
    if (current == FIRING || current == POWERED)
      node.updateDisplay("OFF", "", true);
  });

  node.onApproval([](const char *machine) {
    Log.println("Approval callback");

    if (node.machinestate != MachineState::CHECKINGCARD) {
      Log.println("Ignoring approval - not expected.");
      node.buzzerErr();
      return;
    };

    // Jumper the safety interlock to switch things on; the equivalent
    // of pressing the 'green' button.
    //
    digitalWrite(RELAY_NO_START, HIGH);
    delay(SAFETY_RELAY_BUTTON_PRESS_LENGTH);
    digitalWrite(RELAY_NO_START, LOW);

    // We rely on the change-state callback to see the
    // power on the opto that is connected to the output
    // of all contactor & current-limited; and have that
    // trigger the change change to 'powered'
  });

  Log.println("Starting loop(): " __FILE__ " " __DATE__ " " __TIME__);
}

void loop() {
  node.loop();


  static unsigned long lst = millis();
  static unsigned long cnt = 0;
  if (millis() - lst > 60 * 1000 && cnt != whCounter) {
    Log.printf("kWh meter: %.3f\n", whCounter / 1000.);
    lst = millis(); cnt = whCounter;
  }
}
