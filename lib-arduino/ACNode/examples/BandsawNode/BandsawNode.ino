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

   Compile settings:  ESP32-WROOM-DA Module (or ESP32 Dev)

  QR code shown:

   https://wiki.makerspaceleiden.nl/mediawiki/index.php/QR_lintzaag

   2025/02/10 - changes freom a 1.08 white not to a newer blue board
*/
#include <BlackNodev111.h>

#ifndef MACHINE
#define MACHINE             "lintzaag"
#endif

#define INTERLOCK     (node.OPTO0) // Detect voltage on the interlock/safety contactor.
// #define ONOFFSWITCH   (node.OPTO1) // Detects voltage on the normally-closed circuit of the front switch.
#define MOTOR_CURRENT (node.CURR0) // One of the 3-phase wires to the motor runs through this current coil.

// The relay that sits in the safety interlock of
// the contactor at the back-bottom of the saw.
#define RELAY_GPIO    (node.OUT0)

// Generate with 'echo -n Password | openssl md5 or
// use https://www.md5hashgenerator.com/. No \0,
// cariage return or linefeed  at the end of the
// password; just the characters of the passwordz
// itself.
//
// #define OTA_PASSWD_HASH  "0f475732f6c1a632b3e161160be0cfc5" // the MD5 of "SomethingSecrit"
//
#ifndef OTA_PASSWD_HASH
#error "An OTA password hash(md5) MUST be set. Sorry."
#endif
const char ota_password_hash[] = OTA_PASSWD_HASH;

BlackNodev111 node = BlackNodev111(MACHINE);

unsigned long bad_poweroff = 0, normal_poweroff = 0, normal_poweron = 0, idle_poweroff = 0;

IODebounce *interlockDetect, *motorCurrent, *onoffSwitchDetect;

// Extra state - when the safety contactor has actually been unlocked
// but the RED button has not been pressed yet.
//
MachineState::machinestate_t ACTIVATED;
const unsigned int MAX_SECS_WAIT_FOR_RED_BUTTON = 100;

// Extra state above 'POWERED' - when the saw is spinning (detected via the motorCurrent) as
// opposed to the safety circuitry being powered (i.e. relay has closed, so the interlock
// circuit with the eStop allows the main contactor to be on). We use this for the logic
// of locking the machine off after so many hours of no use.
//
MachineState::machinestate_t RUNNING;
const unsigned int MAX_SECS_IDLE = 3600;

// Extra state af the user has pressed the green button to de-activate the safety
// interlock. To both separate the events for EMC reasons and make the shutdown
// process more explicit/give the users time to change their mind.
//
MachineState::machinestate_t SHUTTINGDOWN;

#ifdef ONOFFSWITCH
IODebounce *onoffSwitchDetect;

// Extra state - in which the machine is off; but the user has switch the operating
// switch to `on'.
MachineState::machinestate_t UNSAFE;
#endif

static void tellOff(const char *msg) {
  node.updateDisplay(msg, "", "");
  Log.printf("Telling=off: %s\n", msg);
  for (int i = 0; i < 9; i++) {
    node.buzzerErr();
    delay(300);
  };
}

class MachineDeck : public Deck {
  public:
    MachineDeck(BlackNodev111 * node) : Deck(node) {};
    void render_pane(bool refresh) {
      _display->clearDisplay();
      _display->print_centred(MACHINE);

#ifdef ONOFFSWITCH
      _display->printf("Operator OnOff Switch\n    %s\n", onoffSwitchDetect->state() ?
                       (interlockDetect->state() ? "unsafe(on)" : "on") : "off");
#endif

      _display->printf("Safety/Interlock\n    %s\n",
                       interlockDetect->state() == LOW ? "ok" : "broken");

      _display->printf("Motor Current/Voltage\n    I=%s V=%s(%s)\n",
                       motorCurrent->state() ? "yes" : "no",
                       node.getMonitoredOutput(RELAY_GPIO) ? "on" : "off",
                       node.monitoredOutputIsOK(RELAY_GPIO) ? "ok" : "FAIL"
                      );
    }
};

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n\n");
  Log.printf("\nBooting(): %s " __DATE__ " " __TIME__ "\n", FILE2FIRMWARE(__FILE__));

  // Init the hardware and get it into a safe state.
  // Init the hardware and get it into a safe state.
  //
  expandedPinMode(RELAY_GPIO, OUTPUT);
  node.setMonitoredOutput(RELAY_GPIO, 0);

  ACTIVATED =  node.machinestate.addState("Waiting 4 Safety", LED::LED_ON,
                                          MAX_SECS_WAIT_FOR_RED_BUTTON * 1000,  MachineState::WAITINGFORCARD, false);
  RUNNING = node.machinestate.addState("Saw Running", LED::LED_ON,
                                       MachineState::NEVER, MachineState::WAITINGFORCARD, false);
  SHUTTINGDOWN =  node.machinestate.addState("Locking machine",
                  LED::LED_ON, 60 * 1000, MachineState::WAITINGFORCARD, false);


#ifdef ONOFFSWITCH
  expandedPinMode(ONOFFSWITCH, INPUT);
  onoffSwitchDetect = new IODebounce("OnOffSwitch", ONOFFSWITCH);
  node.addHandler(onoffSwitchDetect);

  UNSAFE =  node.machinestate.addState("Blocked, switch=ON",
                                       LED::LED_ON,
                                       MachineState::NEVER, MachineState::NEVER, false);
  onoffSwitchDetect->setCallback([](const int newState) {
    if (node.machinestate == MachineState::WAITINGFORCARD && newState) {
      Log.println("OnOff switch in the unsafe 'on' position; locking machine");
      node.machinestate = UNSAFE;
    };
    if (node.machinestate == UNSAFE && !newState) {
      Log.println("OnOff switch in the right, off, position again");
      node.machinestate = MachineState::WAITINGFORCARD;
    }
  });
#endif

  expandedPinMode(INTERLOCK, INPUT);
  interlockDetect = new IODebounce("Interlock", INTERLOCK);
  node.addHandler(interlockDetect);

  interlockDetect->setCallback([](const int newState) {
    if ((node.machinestate == MachineState::CHECKINGCARD || node.machinestate == MachineState::WAITINGFORCARD) && newState == LOW) {
      Log.println("Alert: Power on the interlock observed while " MACHINE " should be locked.");
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
      node.machinestate = SHUTTINGDOWN;
      normal_poweroff++;
    }
    else if (node.machinestate == ACTIVATED && newState == LOW) {
      Log.println("Normal poweron with the red button.");
      node.machinestate = POWERED;
      normal_poweron++;
    }
    else
      Debug.printf("Interlock power now %s (State: %s)\n", newState ? "OFF" : "ON", node.machinestate.label());
  });

  motorCurrent = new IODebounce("MotorCurrent", MOTOR_CURRENT);
  motorCurrent->setAnalogThreshold(30);  // Was 600
  node.addHandler(motorCurrent);

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
  });


  node.setOTAPasswordHash(ota_password_hash);
  node.set_mqtt_prefix("ac");
  node.set_master("master");

  node.setNodeDeck(new MachineDeck(&node));

  node.onReport([](JsonObject  report) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s %s %s", FILE2FIRMWARE(__FILE__), __DATE__, __TIME__);
    report["fw"] = tmp;
    report["bad_poweroff"] = bad_poweroff;
    report["normal_poweroff"] = normal_poweroff;
    report["idle_poweron"] = idle_poweroff;
    report["normal_poweroff"] = normal_poweroff;
  });

  node.begin();

  node.setOnChangeCallback(MachineState::ALL_STATES, [](MachineState::machinestate_t last, MachineState::machinestate_t current) -> void {
    if (current == RUNNING || current == POWERED) {
      // We do not show the 'OFF' button - we expect the user to use
      // the RED/Green on/off button of the safety contactor.
      node.updateDisplay("", "", true);
    };
    if (current == POWERED)
      node.updateDisplayStateMsg("RED @back 4 off", 2);
  });

  node.onApproval([](const char *machine) {
    // We allow 'taking over this machine while it is on' -- hence this check for
    // if it is powered; and in that case -also- accepting a new approval.
    //
#if 0
    if ((node.machinestate != POWERED) &&
        (node.machinestate != MachineState::WAITINGFORCARD) &&
        (node.machinestate != MachineState::CHECKINGCARD) &&
        (node.machinestate != ACTIVATED) &&
        (node.machinestate != SHUTTINGDOWN)
       ) {
      Log.println("Unexpected state - Approved action ignored");
      node.buzzerErr();
      return;
    };
#endif
    Log.println("Action Approved.");
    if (node.machinestate != POWERED)
      node.machinestate = ACTIVATED;
  });

  Log.printf("Starting loop(): %s " __DATE__ " " __TIME__ "\n", FILE2FIRMWARE(__FILE__));
}

void loop() {
  node.loop();

  if (node.machinestate == ACTIVATED || node.machinestate == SHUTTINGDOWN) {
    static unsigned long lst = millis();
    if (millis() - lst > 1000) {
      lst = millis();
      if (node.machinestate == SHUTTINGDOWN)
        node.updateDisplayStateMsg("in", 1);
      else
        node.updateDisplayStateMsg("Prss GREEN @ back", 1);

      node.updateDisplayStateMsg(node.machinestate.timeLeftInThisState().c_str(), 2);
    }
  };

  if (node.machinestate == ACTIVATED && node.machinestate.secondsInThisState() > MAX_SECS_IDLE) {
    Log.println("Power off after beeing idle too long.");
    node.buzzerErr();
    node.machinestate = SHUTTINGDOWN;
    idle_poweroff++;
  };

  node.setMonitoredOutput(RELAY_GPIO,
                          ((node.machinestate == POWERED) || (node.machinestate == RUNNING) || (node.machinestate == ACTIVATED) || (node.machinestate == SHUTTINGDOWN)) ? HIGH : LOW);

  static unsigned long lst = millis();
  if (millis() - lst > 10 * 1000) {
    lst = millis();
#ifdef ONOFFSWITCH
    Debug.printf("Opto2/onOffSwitch(0x%x): %4u(%s(%d))",
                 ONOFFSWITCH, onoffSwitchDetect->raw(), onoffSwitchDetect->state() ? "On " : "Off", onoffSwitchDetect->rawState()
                );
#endif
    Debug.printf("Curr/motorCurrent(0x%x): %4u(%s(%d)) Opto1/Interlock(0x%x): %4u(%s(%d))\n",
                 MOTOR_CURRENT, motorCurrent->raw(), motorCurrent->state() ? "On " : "Off", motorCurrent->rawState(),
                 INTERLOCK, interlockDetect->raw(),  interlockDetect->state() ? "On " : "Off", interlockDetect->rawState()
                );
  }
}
