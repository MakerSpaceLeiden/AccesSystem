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
#include <BlueNodev114.h>

#ifndef ARDUINO_ESP32_WROOM_DA
#error "Black/Blue Hardware is expected to be an ESP32 WROOM-DA"
#endif

#ifndef ARDUINO_PARTITION_min_spiffs
#error "Unexpected partition table; may break OTA"
#endif

#ifndef MACHINE
#define MACHINE "surfacegrinder"  // tafelcircelzaag
#endif

// The relay that sits in the safety interlock of
// the contactor at the back-bottom of the saw.
#define RELAY_GPIO (node.OUT0)

// One of the 3-phase wires to the motor runs through this current coil.
#define MOTOR_CURRENT (node.CURR0)
#define CURR_TRESHOLD (200)

// Generate with 'echo -n Password | openssl md5 or
// use https://www.md5hashgenerator.com/. No \0,
// cariage return or linefeed  at the end of the
// password; just the characters of the passwordz
// itself.
//
// #define OTA_PASSWD_HASH  "0f475732f6c1a632b3e161160be0cfc5" // the MD5 of "SomethingSecrit"
//
#ifndef OTA_PASSWD_HASH256
#error "An OTA password hash(md5) MUST be set. Sorry."
#endif
const char ota_password_hash[] = OTA_PASSWD_HASH256;

BlueNodev114 node = BlueNodev114(MACHINE);
IODebounce *motorCurrent = NULL;

const unsigned int MAX_SECS_IDLE = 3600;

// Extra state above 'POWERED' - when the saw is spinning (detected via the motorCurrent) as
// opposed to the safety circuitry being powered (i.e. relay has closed, so the interlock
// circuit with the eStop allows the main contactor to be on). We use this for the logic
// of locking the machine off after so many hours of no use.
//
MachineState::machinestate_t RUNNING;

class MachineDeck : public Deck {
public:
  MachineDeck(BlackNodev111 *node)
    : Deck(node){};

  void render_pane(bool refresh) {
    _display->clearDisplay();
    _display->print_centred(MACHINE);

    _display->printf("Motor Current/Voltage\n    I=%s V=%s(%s)\n",
                     motorCurrent->state() ? "yes" : "no",
                     node.getMonitoredOutput(RELAY_GPIO) ? "on" : "off",
                     node.monitoredOutputIsOK(RELAY_GPIO) ? "ok" : "FAIL");
  }
};

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n\n");
  Log.printf("\nBooting(): %s " __DATE__ " " __TIME__ "\n", FILE2FIRMWARE(__FILE__));

  expandedPinMode(RELAY_GPIO, OUTPUT);
  node.setMonitoredOutput(RELAY_GPIO, 0);

  // Set an idle poweroff when the machine is on; but the motor is not running.
  // But if the motor is running - we set NEVER.
  node.machinestate.setTimeout(POWERED, MAX_SECS_IDLE * 1000);
  RUNNING = node.machinestate.addState("Running", LED::LED_ON,
                                       MachineState::NEVER, MachineState::WAITINGFORCARD, false);

  motorCurrent = new IODebounce("motor_current", MOTOR_CURRENT);
  motorCurrent->setAnalogThreshold(CURR_TRESHOLD);
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

  node.onReport([](JsonObject report) {
    char buff[128];
    snprintf(buff, sizeof(buff), "%s " __DATE__ " " __TIME__, FILE2FIRMWARE(__FILE__));
    report["fw"] = buff;
  });

  node.begin();

  node.setOnChangeCallback(MachineState::ALL_STATES, [](MachineState::machinestate_t last, MachineState::machinestate_t current) -> void {
    if (current == POWERED) {
      node.updateDisplay("OFF", "", true);  // only show off when you can actually do off.
      node.updateDisplayStateMsg("ON", 2);
    };
    if (current == RUNNING) {
      node.updateDisplayStateMsg("RUNNING", 2);
    };
  });

  node.setOffCallback([](const int newState) -> bool {
    if (node.machinestate != POWERED)
      return false;
    node.buzzerErr();
    node.machinestate = MachineState::WAITINGFORCARD;
    Log.println("Powered off after user button press");
    return true;
  });

  Log.printf("Starting loop(): %s " __DATE__ " " __TIME__ "\n", FILE2FIRMWARE(__FILE__));
}

void loop() {
  node.loop();

  node.setMonitoredOutput(RELAY_GPIO,
                          ((node.machinestate == POWERED) || (node.machinestate == RUNNING)));
}
