/*
      Copyright 2015-2018, 2023 Dirk-Willem van Gulik <dirkx@webweaving.org>
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
#include <ButtonDebounce.h>

#ifndef MACHINE
#define MACHINE "metalmitresaw"
#endif

const unsigned long COOLANT_NAG_TIMEOUT = 60;  // Start nagging after running for over a minute with no coolant.

#define SAFETY (OPTO1)
#define PUMP (OPTO0)
#define RELAY_GPIO (OUT0)
#define MOTOR_CURRENT (CURR0)

WhiteNodev108 node = WhiteNodev108(MACHINE, WIFI_NETWORK, WIFI_PASSWD);

//#define OTA_PASSWD          "SomethingSecrit"
OTA ota = OTA(OTA_PASSWD);

ButtonDebounce *safetyDetect, *pumpDetect, *motorCurrent;

MachineState::machinestate_t RUNNING;

unsigned long bad_poweroff = 0;
unsigned long no_coolant = 0;
unsigned long last_coolant_warn = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n\n");
  Serial.println("Booted: " __FILE__ " " __DATE__ " " __TIME__);

  // Init the hardware and get it into a safe state.
  //
  digitalWrite(RELAY_GPIO, 0);
  pinMode(RELAY_GPIO, OUTPUT);

  RUNNING = node.machinestate.addState("Saw Running", LED::LED_ON, MachineState::NEVER, 0);

  pinMode(PUMP, INPUT);
  pumpDetect = new ButtonDebounce(PUMP);
  pumpDetect->setCallback([](const int newState) {
    // remove nag from screen, if any.
    if (node.machinestate == RUNNING && !newState)
      node.updateDisplay(node.machinestate.label(), "", true);

    Log.printf("Coolant pump now %s\n", newState ? "OFF" : "ON");
  },
                          CHANGE);

  pinMode(SAFETY, INPUT);
  safetyDetect = new ButtonDebounce(SAFETY);
  safetyDetect->setCallback([](const int newState) {
    if (node.machinestate == CHECKINGCARD && newState == LOW)
      node.machinestate = FAULTED;
    if (node.machinestate == FAULTED && newState == HIGH)
      node.machinestate = MachineState::WAITINGFORCARD;
    Log.printf("Interlock power now %s\n", newState ? "OFF" : "ON");
  },
                            CHANGE);

  motorCurrent = new ButtonDebounce(MOTOR_CURRENT);
  motorCurrent->setAnalogThreshold(600); // typical is 0-50 for off, 1200 for on.
  motorCurrent->setCallback([](const int newState) {
    if (node.machinestate == POWERED && newState) {
      Log.println("Detected current. Motor switched on");
      node.machinestate = RUNNING;
    } else if (node.machinestate == RUNNING && !newState) {
      Log.println("No more current; motor no longer on.");
      node.machinestate = POWERED;
    } else {
      Log.printf("Unexpected change in motor current; state is %s and the current is %s\n",
                 node.machinestate.label(), newState ? "ON" : "OFF");
    }
  },
                            CHANGE);

  node.setOffCallback([](const int newState) {
    if (node.machinestate == RUNNING) {
      // Refuse to let the safety be used to power something off.
      Log.println("Bad poweroff attempt. Ignoring.");
      digitalWrite(BUZZER, HIGH);
      delay(500);
      digitalWrite(BUZZER, LOW);
      bad_poweroff++;
      return;
    };
    Log.println("Left button press ignored.");
  },
                      WHEN_PRESSED);

  node.setMenuCallback([](const int newState) {
    Log.println("Right button press ignored.");
  },
                       WHEN_PRESSED);

  node.setOnChangeCallback(MachineState::ALL_STATES, [](MachineState::machinestate_t last, MachineState::machinestate_t current) -> void {
    if (current == RUNNING) {
      node.updateDisplay(node.machinestate.label(), "", true);
      last_coolant_warn = 0;
    }
  });

  node.onSwipe([](const char *tag) -> ACBase::cmd_result_t {
    digitalWrite(BUZZER, HIGH);
    delay(20);
    digitalWrite(BUZZER, LOW);
    // Let the core library handle the rest.
    return ACBase::CMD_DECLINE;
  });

  node.set_mqtt_prefix("ac");
  node.set_master("master");

  node.onConnect([]() {
    node.machinestate = MachineState::WAITINGFORCARD;
  });
  node.onDisconnect([]() {
    node.machinestate = MachineState::NOCONN;
  });
  node.onError([](acnode_error_t err) {
    Log.printf("Error %d\n", err);
    node.machinestate = MachineState::TRANSIENTERROR;
  });
  node.onApproval([](const char *machine) {
    Log.println("Approval callback");
    if (node.machinestate != POWERED) {
      node.buzzerErr();
    };
    node.machinestate = POWERED;
  });
  node.onDenied([](const char *machine) {
    if (node.machinestate == SCREENSAVER) {
      node.machinestate = MachineState::WAITINGFORCARD;
      return;
    };
    node.machinestate = MachineState::REJECTED;
    node.buzzerErr();
  });

  node.onReport([](JsonObject &report) {
    report["bad_poweroff"] = bad_poweroff;
    report["no_coolant_longruns"] = no_coolant;
  });

#ifdef OTA_PASSWD
  node.addHandler(&ota);
#endif

  node.begin();

  Log.println("Booted: " __FILE__ " " __DATE__ " " __TIME__);
}

void loop() {
  node.loop();

 if (0)  {
    static unsigned long lst = 0;
    if (millis() - lst > 5000) {
      lst = millis();
      Log.printf("Motor current: %d 0=%d 1=%d\n", motorCurrent->rawState(),analogRead(CURR0), analogRead(CURR1));
    };
  }
  if ((node.machinestate == RUNNING) && (node.machinestate.secondsInThisState() > COOLANT_NAG_TIMEOUT) && (pumpDetect != 0) && (last_coolant_warn == 0)) {
    // We've been running for a fair bit; but still
    // no coolant. Warn once during each lengthy run.
    //
    Log.println("Coolant pump is off during lengthy use.");

    last_coolant_warn = millis();
    node.updateDisplayStateMsg("COOLANT ?!");

    for (int i = 0; i < 10; i++) node.buzzerErr();
    no_coolant++;
  }

  digitalWrite(RELAY_GPIO,
               ((node.machinestate == POWERED) || (node.machinestate == RUNNING)) ? HIGH : LOW);
}
