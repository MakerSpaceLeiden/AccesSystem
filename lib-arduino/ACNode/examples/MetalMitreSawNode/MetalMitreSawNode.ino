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

const unsigned long CARD_CHECK_WAIT = 3;              // wait up to 3 seconds for a card to be checked.
const unsigned long MAX_IDLE_TIME = 45 * 60;          // auto power off the machine after 45 minutes of no use.
const unsigned long SHOW_COUNTDOWN_TIME_AFTER = 600;  // Only start showing above idle to off countdown after 10 minutes of no use.
const unsigned long SCREENSAVER_DELAY = 20 * 60;      // power off the screen after some period of no swipe/interaction.

#define SAFETY (OPTO0)
#define PUMP (OPTO1)
#define RELAY_GPIO (OUT0)
#define CURRENT_COIL (CURR0)

#define MENU_BUTTON (BUTT0)
#define OFF_BUTTON (BUTT1)
#define WHEN_PRESSED (ONLOW)  // pullup, active low buttons

//#define OTA_PASSWD          "SomethingSecrit"
WhiteNodev108 node = WhiteNodev108(MACHINE, WIFI_NETWORK, WIFI_PASSWD);
OTA ota = OTA(OTA_PASSWD);

LED errorLed = LED(LED_INDICATOR);

ButtonDebounce *offButton, *menuButton, *safetyDetect, *pumpDetect, *currentCoil;

MachineState machinestate = MachineState();

// Extra, hardware specific states
MachineState::machinestate_t FAULTED, SCREENSAVER, INFODISPLAY, POWERED, RUNNING, CHECKINGCARD;

unsigned long powered_total = 0, powered_last;
unsigned long running_total = 0, running_last;
unsigned long bad_poweroff = 0;
unsigned long idle_poweroff = 0;
unsigned long manual_poweroff = 0;
unsigned long errors = 0;
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

  digitalWrite(BUZZER, LOW);
  pinMode(BUZZER, OUTPUT);

  FAULTED = machinestate.addState("Switch Fault", LED::LED_ERROR, MachineState::NEVER,0);
  CHECKINGCARD = machinestate.addState("Checking card....", LED::LED_OFF, CARD_CHECK_WAIT * 1000, MachineState::WAITINGFORCARD);
  SCREENSAVER = machinestate.addState("Waiting for card, screen dark", LED::LED_OFF, MachineState::NEVER,0);
  INFODISPLAY = machinestate.addState("User browsing info pages", LED::LED_OFF, 20 * 1000, MachineState::WAITINGFORCARD);
  POWERED = machinestate.addState("Powered but idle", LED::LED_ON, MAX_IDLE_TIME * 1000, MachineState::WAITINGFORCARD);
  RUNNING = machinestate.addState("Saw Running", LED::LED_ON, MachineState::NEVER, 0);

  machinestate.setState(MachineState::BOOTING);

  pinMode(PUMP, INPUT);  // optocoupler has its own pullup
  pumpDetect = new ButtonDebounce(PUMP);
  pumpDetect->setAnalogThreshold(2500);
  pumpDetect->setCallback([](const int newState) {
    Log.printf("Coolant pump now %s\n", newState ? "ON" : "OFF");
  },
                          CHANGE);

  pinMode(SAFETY, INPUT);  // optocoupler has its own pullup
  safetyDetect = new ButtonDebounce(SAFETY);
  safetyDetect->setAnalogThreshold(2500);
  safetyDetect->setCallback([](const int newState) {
    if (machinestate == MachineState::WAITINGFORCARD && newState) {
      Log.println("Detecting that the switch on the machine is on while we're in swipe-card mode.");
      machinestate = FAULTED;
    };
    if (machinestate == FAULTED && !newState) {
      machinestate = MachineState::WAITINGFORCARD;
      Log.println("Clearing fault - switch in safe off-position.");
    };
    Log.printf("Interlock power now %s\n", newState ? "OFF" : "ON");
  },
                            CHANGE);

  currentCoil = new ButtonDebounce(CURRENT_COIL);
  currentCoil->setAnalogThreshold(500);
  currentCoil->setCallback([](const int newState) {
    if (machinestate == RUNNING && !newState) {
      machinestate = POWERED;
      Log.println("Machine stopped");
      return;
    };
    if (machinestate == POWERED && newState) {
      Log.println("Machine turned on");
      machinestate = RUNNING;
      return;
    };
    Log.printf("Odd current change (current=%s, state=%s). Ignored.\n",
               newState ? "present" : "absent", machinestate.label());
  });

  pinMode(OFF_BUTTON, INPUT_PULLUP);
  offButton = new ButtonDebounce(OFF_BUTTON);
  offButton->setCallback([](const int newState) {
    Log.println("Off button pressed");
    if (machinestate == SCREENSAVER) {
      machinestate = MachineState::WAITINGFORCARD;
      return;
    };
    if (machinestate == RUNNING) {
      // Refuse to let the safety be used to power something off.
      Log.println("Bad poweroff attempt. Ignoring.");
      digitalWrite(BUZZER, HIGH);
      delay(500);
      digitalWrite(BUZZER, LOW);
      bad_poweroff++;
      return;
    };
    if (machinestate == POWERED) {
      Log.println("Machine powered down by OFF button");
      machinestate = MachineState::WAITINGFORCARD;
      digitalWrite(BUZZER, HIGH);
      delay(50);
      digitalWrite(BUZZER, LOW);
      manual_poweroff++;
      return;
    };
    if (machinestate == INFODISPLAY) {
      machinestate = MachineState::WAITINGFORCARD;
      return;
    };
    Log.println("Left button press ignored.");
  },
                         WHEN_PRESSED);

  pinMode(MENU_BUTTON, INPUT_PULLUP);
  menuButton = new ButtonDebounce(MENU_BUTTON);
  menuButton->setCallback([](const int newState) {
    Log.println("Menu button pressed");
    if (machinestate == SCREENSAVER) {
      machinestate = MachineState::WAITINGFORCARD;
      return;
    };
    if (machinestate == MachineState::WAITINGFORCARD) {
      machinestate = INFODISPLAY;
    }
    Log.println("Right button press ignored.");
  },
                          WHEN_PRESSED);



  machinestate.setOnChangeCallback(MachineState::ALL_STATES, [](MachineState::machinestate_t last, MachineState::machinestate_t current) -> void {
    Log.printf("Changing state (%d->%d): %s\n", last, current, machinestate.label());

    node.setDisplayScreensaver(current == SCREENSAVER);
    if (current == FAULTED) {
      node.updateDisplay("", "", true);
      Log.println("Machine poweron disabled - macine on/off switch in the 'on' position.");
      errors++;
    } else if (current == MachineState::WAITINGFORCARD) {
      node.updateDisplay("", "MORE", true);
      if (safetyDetect == LOW)
        machinestate = FAULTED;
    } else if (current == CHECKINGCARD)
      node.updateDisplay("", "", true);
    else if (current == POWERED)
      node.updateDisplay("TURN OFF", "", true);
    else if (current == RUNNING) {
      node.updateDisplay(machinestate.label(), "", true);
      last_coolant_warn = 0;
    } else if (current == INFODISPLAY) {
      node.updateInfoDisplay(0);
      return;
    }
    node.updateDisplayStateMsg(machinestate.label());
  });


  node.onSwipe([](const char *tag) -> ACBase::cmd_result_t {
    if (machinestate < MachineState::WAITINGFORCARD) {
      Log.printf("Ignoring swipe; as the node is not yet ready for it\n");
      return ACBase::CMD_CLAIMED;
    };
    if (machinestate == SCREENSAVER) {
      machinestate.setState(MachineState::WAITINGFORCARD);
      Log.println("Switching off the screensaver");
    };
    if (machinestate == INFODISPLAY) {
      machinestate.setState(MachineState::WAITINGFORCARD);
      Log.println("Aborting info, to handle swipe");
    };
    digitalWrite(BUZZER, HIGH);
    delay(20);
    digitalWrite(BUZZER, LOW);
    machinestate = CHECKINGCARD;
    // Let the core library handle the rest.
    return ACBase::CMD_DECLINE;
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
  node.onApproval([](const char *machine) {
    Log.println("Approval callback");
    if (machinestate != POWERED) {
      digitalWrite(BUZZER, HIGH);
      delay(50);
      digitalWrite(BUZZER, LOW);
    };
    machinestate.setState(POWERED);
  });
  node.onDenied([](const char *machine) {
    if (machinestate == SCREENSAVER) {
      machinestate.setState(MachineState::WAITINGFORCARD);
      return;
    };
    machinestate.setState(MachineState::REJECTED);
    for (int i = 0; i < 10; i++) {
      digitalWrite(BUZZER, HIGH);
      delay(50);
      digitalWrite(BUZZER, LOW);
      delay(50);
    };
  });

  node.onReport([](JsonObject &report) {
    report["idle_poweroff"] = idle_poweroff;
    report["bad_poweroff"] = bad_poweroff;
    report["manual_poweroff"] = manual_poweroff;

    report["no_coolant_longruns"] = no_coolant;

    report["errors"] = errors;
  });

#ifdef OTA_PASSWD
  node.addHandler(&ota);
#endif
  node.addHandler(&machinestate);

  node.begin();

  Log.println("Booted: " __FILE__ " " __DATE__ " " __TIME__);
}

void loop() {
  node.loop();
  errorLed.set(machinestate.ledState());

  static unsigned long lst = 0;
  if (millis() - lst > 1000) {
    lst = millis();
    if (0) Log.printf("C0=%d\tC1=%d\tO0=%d\tO1=%d\n", analogRead(CURR0), analogRead(CURR1), analogRead(OPTO0), analogRead(OPTO1));

    if (machinestate == POWERED) {
      String left = machinestate.timeLeftInThisState();
      // Show the countdown to poweroff; only when the machine
      // has been idle for a singificant bit of time.
      //
      if (left.length() && machinestate.secondsInThisState() > SHOW_COUNTDOWN_TIME_AFTER) node.updateDisplayStateMsg("Auto off: " + left, 1);
    };

    if ((machinestate == MachineState::WAITINGFORCARD) && (machinestate.secondsInThisState() > SCREENSAVER_DELAY)) {
      Log.println("Enabling screensaver");
      machinestate.setState(SCREENSAVER);
    };
  };

  if ((machinestate == RUNNING) && (machinestate.secondsInThisState() > 120) && (pumpDetect == LOW) && (last_coolant_warn == 0)) {
    // We've been running for a fair bit; but still no coolant.
    no_coolant++;
    Log.println("Coolant pump is off during lengthy use.");
    last_coolant_warn = millis();
    node.updateDisplayStateMsg("COOLANT ?!");
    for (int i = 0; i < 10; i++) {
      digitalWrite(BUZZER, HIGH);
      delay(100);
      digitalWrite(BUZZER, LOW);
    };
  }

  digitalWrite(RELAY_GPIO,
               ((machinestate == POWERED) || (machinestate == RUNNING)) ? HIGH : LOW);
}
