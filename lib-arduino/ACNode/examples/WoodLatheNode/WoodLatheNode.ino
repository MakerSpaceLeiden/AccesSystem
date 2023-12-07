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

   Compile settings:  EPS32-WROOM-DA
*/
#include <WhiteNodev108.h>
#include <TimerEvent.h>

#ifndef MACHINE
#define MACHINE   "woodlathe"
#endif

#define MAX_IDLE_TIME       (35 * 60 * 1000) // auto power off after 35 minutes of no use.

// #define INTERLOCK      (OPTO2)
#define MENU_BUTTON          (BUTT0)
#define OFF_BUTTON          (BUTT1)
#define RELAY_GPIO          (OUT0)

//#define OTA_PASSWD          "SomethingSecrit"
WhiteNodev108 node = WhiteNodev108(MACHINE);

#ifdef OTA_PASSWD
OTA ota = OTA(OTA_PASSWD);
#endif

// LED aartLed = LED(LED_INDICATOR);

MachineState machinestate = MachineState();

// Extra, hardware specific states
MachineState::machinestate_t POWERED, RUNNING;

unsigned long powered_total = 0, powered_last;
unsigned long running_total = 0, running_last;
unsigned long bad_poweroff = 0;
unsigned long idle_poweroff = 0;
unsigned long manual_poweroff = 0;
unsigned long errors = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n\n");
  Serial.println("Booted: " __FILE__ " " __DATE__ " " __TIME__ );

  // Init the hardware and get it into a safe state.
  //
  digitalWrite(RELAY_GPIO, 0);
  pinMode(RELAY_GPIO, OUTPUT);

  digitalWrite(BUZZER, LOW);
  pinMode(BUZZER, OUTPUT);

  pinMode(MENU_BUTTON, INPUT_PULLUP);
  pinMode(OFF_BUTTON, INPUT_PULLUP);

  POWERED = machinestate.addState( "Powered but idle", LED::LED_ON, MAX_IDLE_TIME, MachineState::WAITINGFORCARD);
  RUNNING = machinestate.addState( "Lathe Running", LED::LED_ON, MachineState::NEVER);

  machinestate.setState(MachineState::BOOTING);

  machinestate.setOnChangeCallback(MachineState::ALL_STATES, [](MachineState::machinestate_t last, MachineState::machinestate_t current) -> void {
    node.updateDisplayStateMsg(machinestate.label());
    Log.printf("Changing state (%d->%d): %s\n", last, current, machinestate.label());
    if (current == MachineState::WAITINGFORCARD)
      node.updateDisplay("", "MORE", true);
    if (current == POWERED)
      node.updateDisplay("TURN OFF", "MORE", true);
    if (current == RUNNING)
      node.updateDisplay("", "", true);
  });

  node.set_mqtt_prefix("ac");
  node.set_master("master");

  node.onConnect([]() {
    machinestate.setState(MachineState::WAITINGFORCARD);
  });
  node.onDisconnect([]() {
    machinestate.setState(MachineState::NOCONN);
  });
  node.onError([](acnode_error_t err) {
    Log.printf("Error %d\n", err);
    machinestate.setState(MachineState::TRANSIENTERROR);
    errors++;
  });
  node.onApproval([](const char * machine) {
    Log.println("Approval callback");
    if (machinestate.state() != POWERED) {
      digitalWrite(BUZZER, HIGH); delay(50); digitalWrite(BUZZER, LOW);
    };
    machinestate.setState(POWERED);
  });

  node.onDenied([](const char * machine) {
    machinestate.setState(MachineState::REJECTED);
    for (int i = 0; i < 10; i++) {
      digitalWrite(BUZZER, HIGH); delay(100); digitalWrite(BUZZER, LOW); delay(100);
    };
  });

  node.onReport([](JsonObject  & report) {
#ifdef INTERLOCK
    report["interlock"] = digitalRead(INTERLOCK) ? true : false;
#endif
    report["powered_time"] = powered_total + ((machinestate == POWERED) ? ((millis() - powered_last) / 1000) : 0);
    report["running_time"] = running_total + ((machinestate == RUNNING) ? ((millis() - running_last) / 1000) : 0);

    report["idle_poweroff"] = idle_poweroff;
    report["bad_poweroff"] = bad_poweroff;
    report["manual_poweroff"] = manual_poweroff;

    report["errors"] = errors;
  });

#ifdef OTA_PASSWD
  node.addHandler(&ota);
#endif
  node.addHandler(&machinestate);

  node.begin();
  Log.println("Booted: " __FILE__ " " __DATE__ " " __TIME__ );

}

void loop() {
  static unsigned long lst = 0;
  if (millis() - lst > 1000) {
    lst = millis();

    if (machinestate.state() == POWERED) {
      String left = machinestate.timeLeftInThisState();
      if (left.length()) node.updateDisplayStateMsg("Auto off: " + left);
    };

    if (0) Log.printf("off=%d, menu=%d, opto=%d,%d, cur=%d,%d curA=%d,%d\n",
                        digitalRead(OFF_BUTTON), digitalRead(MENU_BUTTON),
                        digitalRead(OPTO0), digitalRead(OPTO1),
                        digitalRead(CURR0), digitalRead(CURR1),
                        analogRead(CURR0), analogRead(CURR1));
  };
  node.loop();

#ifdef INTERLOCK
  if (digitalRead(INTERLOCK)) {
    static unsigned long last_warning = 0;
    if (machinestate != MachineState::OUTOFORDER || last_warning == 0 || millis() - last_warning > 60 * 1000) {
      Log.printf("Problem with the interlock -- is the big green connector unseated ?\n");
      last_warning = millis();
    }
    machinestate = MachineState::OUTOFORDER;
  };
#endif

#ifdef OFF_BUTTON
  if (digitalRead(OFF_BUTTON) == LOW) {
    if (machinestate < POWERED)
      return; // button not yet function.
    if (machinestate > POWERED) {
      // Refuse to let the safety be used to power something off.
      digitalWrite(BUZZER, HIGH); delay(500); digitalWrite(BUZZER, LOW);
      bad_poweroff++;
      return;
    }
    Log.println("Machine powered down by OFF button");
    machinestate.setState(MachineState::WAITINGFORCARD);
    digitalWrite(BUZZER, HIGH); delay(50); digitalWrite(BUZZER, LOW);
    manual_poweroff++;
  };
#endif


  digitalWrite(RELAY_GPIO,
               ((machinestate.state() == POWERED) || (machinestate.state() == RUNNING)) ? HIGH : LOW);
}
