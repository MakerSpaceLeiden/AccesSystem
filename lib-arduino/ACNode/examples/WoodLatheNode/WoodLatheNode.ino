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
#include <ButtonDebounce.h>

#ifndef MACHINE
#define MACHINE   "woodlathe"
#endif

const unsigned long MAX_IDLE_TIME =     35 * 60;  // auto power off the machine after 35 minutes of no use.
const unsigned long SCREENSAVER_DELAY = 20 * 60;  // power off the screen after 20 mins of no swipe.

// #define INTERLOCK      (OPTO2)
#define MENU_BUTTON       (BUTT0)
#define OFF_BUTTON        (BUTT1)
#define RELAY_GPIO        (OUT0)
#define WHEN_PRESSED      (ONLOW) // pullup, active low buttons

//#define OTA_PASSWD          "SomethingSecrit"
WhiteNodev108 node = WhiteNodev108(MACHINE);

#ifdef OTA_PASSWD
OTA ota = OTA(OTA_PASSWD);
#endif

// LED aartLed = LED(LED_INDICATOR);

ButtonDebounce * offButton, * menuButton;

MachineState machinestate = MachineState();

// Extra, hardware specific states
MachineState::machinestate_t SCREENSAVER, INFODISPLAY, POWERED, RUNNING;


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

  SCREENSAVER = machinestate.addState( "Waiting for card, screen dark", LED::LED_OFF, MachineState::NEVER);
  INFODISPLAY = machinestate.addState( "User browsing info pages", LED::LED_OFF, 20 * 1000,  MachineState::WAITINGFORCARD);
  POWERED = machinestate.addState( "Powered but idle", LED::LED_ON, MAX_IDLE_TIME * 1000, MachineState::WAITINGFORCARD);
  RUNNING = machinestate.addState( "Lathe Running", LED::LED_ON, MachineState::NEVER);

  machinestate.setState(MachineState::BOOTING);

  offButton = new ButtonDebounce(OFF_BUTTON);
  offButton->setCallback([](const int newState) {
    if (machinestate == SCREENSAVER) {
      machinestate = MachineState::WAITINGFORCARD;
      return;
    };
    if (machinestate == RUNNING) {
      // Refuse to let the safety be used to power something off.
      Log.println("Bad poweroff attempt. Ignoring.");
      digitalWrite(BUZZER, HIGH); delay(500); digitalWrite(BUZZER, LOW);
      bad_poweroff++;
      return;
    };
    if (machinestate == POWERED) {
      Log.println("Machine powered down by OFF button");
      machinestate = MachineState::WAITINGFORCARD;
      digitalWrite(BUZZER, HIGH); delay(50); digitalWrite(BUZZER, LOW);
      manual_poweroff++;
      return;
    };
    if (machinestate == INFODISPLAY) {
      machinestate = MachineState::WAITINGFORCARD;
      return;
    };
    Log.println("Left button press ignored.");
  }, WHEN_PRESSED);

  menuButton = new ButtonDebounce(MENU_BUTTON);
  menuButton->setCallback([](const int newState) {
    if (machinestate == SCREENSAVER) {
      machinestate = MachineState::WAITINGFORCARD;
      return;
    };
    if (machinestate == MachineState::WAITINGFORCARD) {
      machinestate = INFODISPLAY;
    }
    Log.println("Right button press ignored.");
  }, WHEN_PRESSED);



  machinestate.setOnChangeCallback(MachineState::ALL_STATES, [](MachineState::machinestate_t last, MachineState::machinestate_t current) -> void {
    Log.printf("Changing state (%d->%d): %s\n", last, current, machinestate.label());

    node.setDisplayScreensaver(current == SCREENSAVER);

    if (current == MachineState::WAITINGFORCARD)
      node.updateDisplay("", "MORE", true);
    else if (current == POWERED)
      node.updateDisplay("TURN OFF", "MORE", true);
    else if (current == RUNNING)
      node.updateDisplay(machinestate.label(), "", true);
    else if (current == INFODISPLAY) {
      node.updateInfoDisplay(0);
      return;
    };
    node.updateDisplayStateMsg(machinestate.label());
  });


  node.onSwipe([](const char *tag)  -> ACBase::cmd_result_t  {
    if (machinestate < MachineState::WAITINGFORCARD) {
      Log.printf("Ignoring swipe; as the node is not yet ready for it\n");
      return  ACBase::CMD_CLAIMED;
    };

    if (machinestate == SCREENSAVER) {
      machinestate.setState(MachineState::WAITINGFORCARD);
      Log.println("Switching off the screensaver");
    };
    if (machinestate == INFODISPLAY) {
      machinestate.setState(MachineState::WAITINGFORCARD);
      Log.println("Aborting info, to handle swipe");
    };

    // Let the core library handle the rest.
    return  ACBase::CMD_DECLINE;
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
    if (machinestate != POWERED) {
      digitalWrite(BUZZER, HIGH); delay(50); digitalWrite(BUZZER, LOW);
    };
    machinestate.setState(POWERED);
  });
  node.onDenied([](const char * machine) {
    if (machinestate == SCREENSAVER) {
      machinestate.setState(MachineState::WAITINGFORCARD);
      return;
    };
    machinestate.setState(MachineState::REJECTED);
    for (int i = 0; i < 10; i++) {
      digitalWrite(BUZZER, HIGH); delay(50); digitalWrite(BUZZER, LOW); delay(50);
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
  node.loop();

  static unsigned long lst = 0;
  if (millis() - lst > 1000) {
    lst = millis();

    if (machinestate == POWERED) {
      String left = machinestate.timeLeftInThisState();
      if (left.length() && machinestate.secondsInThisState() > 300) node.updateDisplayStateMsg("Auto off: " + left,1);
    };

    if (machinestate == MachineState::WAITINGFORCARD && machinestate.secondsInThisState() > SCREENSAVER_DELAY) {
      Log.println("Enabling screensaver");
      machinestate.setState(SCREENSAVER);
    };
  };

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

  digitalWrite(RELAY_GPIO,
               ((machinestate == POWERED) || (machinestate == RUNNING)) ? HIGH : LOW);
}
