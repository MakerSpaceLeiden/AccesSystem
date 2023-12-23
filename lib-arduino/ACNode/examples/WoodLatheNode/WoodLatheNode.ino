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

*/
#include <WhiteNodev108.h>
#include <ButtonDebounce.h>
// #include <CurrentTransformer.h>

#ifndef MACHINE
#define MACHINE   "woodlathe"
#endif

const unsigned long CARD_CHECK_WAIT =         3;  // wait up to 3 seconds for a card to be checked.
const unsigned long MAX_IDLE_TIME =     35 * 60;  // auto power off the machine after 35 minutes of no use.
const unsigned long SHOW_COUNTDOWN_TIME_AFTER = 600; // Only start showing above idle to off countdown after 10 minutes of no use.
const unsigned long SCREENSAVER_DELAY = 20 * 60;  // power off the screen after some period of no swipe/interaction.

// #define INTERLOCK      (OPTO2)
#define OFF_BUTTON        (BUTT0)
#define RELAY_GPIO        (OUT0)
#define WHEN_PRESSED      (ONLOW) // pullup, active low buttons
#define CURRENT_COIL      (CURR0)

WhiteNodev108 node = WhiteNodev108(MACHINE);

#ifdef OTA_PASSWD
OTA ota = OTA(OTA_PASSWD);
#endif

LED errorLed = LED(LED_INDICATOR);

ButtonDebounce * offButton;
ButtonDebounce * currentCoil;
// CurrentTransformerWithCallbacks * currentCoil;

MachineState machinestate = MachineState(&errorLed);

// Extra, hardware specific states
MachineState::machinestate_t POWERED, RUNNING, CHECKINGCARD;

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

  pinMode(LED_INDICATOR, OUTPUT);
  machinestate.setState(MachineState::BOOTING);

  CHECKINGCARD = machinestate.addState( "Checking card....", LED::LED_OFF, CARD_CHECK_WAIT * 1000, MachineState::WAITINGFORCARD);
  POWERED = machinestate.addState( "Powered but idle", LED::LED_ON, MAX_IDLE_TIME * 1000, MachineState::WAITINGFORCARD);
  RUNNING = machinestate.addState( "Lathe Running", LED::LED_ON, MachineState::NEVER, 0);

  pinMode(OFF_BUTTON, INPUT_PULLUP);
  offButton = new ButtonDebounce(OFF_BUTTON);
  offButton->setCallback([](const int newState) {
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
    Log.println("Left button press ignored.");
  }, WHEN_PRESSED);


  pinMode(CURRENT_COIL, INPUT);

  //  currentCoil = new CurrentTransformerWithCallbacks(CURRENT_COIL);
  //  currentCoil->setOnLimit(0.002); // we should remove that resistor
  //  currentCoil->onCurrentChange([](const int newState) {

  currentCoil  = new ButtonDebounce(CURRENT_COIL);
  currentCoil->setCallback([](const int newState) {
    //    if (machinestate == RUNNING && newState == CurrentTransformerWithCallbacks::OFF) {
    if (machinestate == RUNNING && !newState) {
      machinestate = POWERED;
      Log.println("Machine stopped");
      return;
    };
    //    if (machinestate == POWERED && newState == CurrentTransformerWithCallbacks::ON) {
    if (machinestate == POWERED && newState) {
      Log.println("Machine turned on");
      machinestate = RUNNING;
      return;
    };
    Log.printf("Odd current change (current=%s, state=%s). Ignored.\n",
               newState ? "present" : "absent", machinestate.label());
  });

  node.onSwipe([](const char *tag)  -> ACBase::cmd_result_t  {
    if (machinestate < MachineState::WAITINGFORCARD) {
      Log.printf("Ignoring swipe; as the node is not yet ready for it\n");
      return  ACBase::CMD_CLAIMED;
    };
    digitalWrite(BUZZER, HIGH); delay(20); digitalWrite(BUZZER, LOW);
    machinestate = CHECKINGCARD;

    // Let the core library handle the rest.
    return  ACBase::CMD_DECLINE;
  });

  node.set_mqtt_prefix("ac");
  node.set_master("master");

  node.onConnect([]() {
    // Kludge - until we understand how the ethernet and
    // powerswitching interacts. Does honor the principle
    // to not interfere with the machine when it is running.
    if (machinestate.state() < MachineState::WAITINGFORCARD)
      machinestate.setState(MachineState::WAITINGFORCARD);
    else
      Log.println("Ignore network (re)connecting");
  });
  node.onDisconnect([]() {
    // Kludge - until we understand how the ethernet and
    // powerswitching interacts. Does honor the principle
    // to not interfere with the machine when it is running.
    if (machinestate.state() <= MachineState::WAITINGFORCARD)
      machinestate.setState(MachineState::NOCONN);
    else
      Log.println("Ignoring disconnect of network.");
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
    machinestate.setState(MachineState::REJECTED);
    for (int i = 0; i < 10; i++) {
      digitalWrite(BUZZER, HIGH); delay(50); digitalWrite(BUZZER, LOW); delay(50);
    };
  });

  node.onReport([](JsonObject  & report) {
#ifdef INTERLOCK
    report["interlock"] = digitalRead(INTERLOCK) ? true : false;
#endif
    report["idle_poweroff"] = idle_poweroff;
    report["bad_poweroff"] = bad_poweroff;
    report["manual_poweroff"] = manual_poweroff;

    report["errors"] = errors;
  });

  node.addHandler(&ota);
  node.addHandler(&machinestate);

  node.begin(false /* this unit has no display */);

  Log.println("Booted: " __FILE__ " " __DATE__ " " __TIME__ );
}

void loop() {
  node.loop();
  currentCoil->loop();

  { static unsigned long lst = 0;
    if (millis() - lst > 2000) {
      lst = millis();
      Log.printf("dC0=%d\tdC1=%d\tdO0=%d\tdO1=%d\n",
                 digitalRead(CURR0), digitalRead(CURR1), digitalRead(OPTO0), digitalRead(OPTO1));
    }
  }

#ifdef INTERLOCK
  if (digitalRead(INTERLOCK)) {
    static unsigned long last_warning = 0;
    if (machinestate != MachineState::OUTOFORDER || last_warning == 0 || millis() - last_warning > 60 * 1000)
      Log.printf("Problem with the interlock -- is the big green connector unseated ?\n");
      last_warning = millis();
    }
    machinestate = MachineState::OUTOFORDER;
  };
#endif

  digitalWrite(RELAY_GPIO,
               ((machinestate == POWERED) || (machinestate == RUNNING)) ? HIGH : LOW);
}
