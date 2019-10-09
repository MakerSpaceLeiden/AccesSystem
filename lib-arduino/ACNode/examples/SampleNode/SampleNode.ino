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
*/
#include <PowerNodeV11.h> // -- this is an olimex board.

#include <WiFiClientSecure.h>
#include <Wire.h>

#include <ACNode.h>
#include "MachineState.h"
#include <ButtonDebounce.h>

#ifndef OTA_PASSWD
#define OTA_PASSWD "Foo"
#warning "Setting easy to guess/hardcoded OTA password."
#endif

// #define WIFI_NETWORK "Foo"
// #define WIFI_PASSWD "Foo"

// #include "acmerootcert.h"

#define MACHINE             "sample"

const uint8_t AARTLED_GPIO  = 15; // Ext 2, pin 8
const uint8_t PUSHBUTTON_GPIO =  1; // Ext 1, pin 6

// ACNode node = ACNode(MACHINE, WIFI_NETWORK, WIFI_PASSWD);
ACNode node = ACNode(MACHINE);

LED aartLed = LED(AARTLED_GPIO);    // defaults to the aartLed - otherwise specify a GPIO.
ButtonDebounce button(PUSHBUTTON_GPIO, 250);

OTA ota = OTA(OTA_PASSWD);

MqttLogStream mqttlogStream = MqttLogStream();
TelnetSerialStream telnetSerialStream = TelnetSerialStream();

MachineState machinestate = MachineState();
MachineState::machinestates_t BUTTON_PRESSED, ACTIVE, BORED;

unsigned long button_count = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n\n");
  Serial.println("Booted: " __FILE__ " " __DATE__ " " __TIME__ );

  pinMode(PUSHBUTTON_GPIO, INPUT_PULLUP);
  aartLed.set(LED::LED_FAST);

  node.set_mqtt_prefix("test");
  node.set_master("master");

  BORED = machinestate.addState("Done being active", LED::LED_ERROR, 5 * 1000, MachineState::WAITINGFORCARD);
  ACTIVE = machinestate.addState("Very active now for 5 seconds", LED::LED_ERROR, 5 * 1000, BORED);
  BUTTON_PRESSED = machinestate.addState("Going active", LED::LED_IDLE, 1 * 1000, ACTIVE);

  // Update the display whenever we enter into a new state.
  //
  // machinestate.setOnChangeCallback(MachineState::ALL_STATES, [](MachineState::machinestates_t last, MachineState::machinestates_t current) -> void {
  //  Debug.println(...
  // });

  node.onConnect([]() {
    Log.println("Connected");
    machinestate = MachineState::WAITINGFORCARD;
  });
  node.onDisconnect([]() {
    Log.println("Disconnected");
    machinestate = MachineState::NOCONN;

  });
  node.onError([](acnode_error_t err) {
    Log.printf("Error %d\n", err);
    machinestate = MachineState::WAITINGFORCARD;
  });

  node.onReport([](JsonObject  & report) {
    report["button_count"] = button_count;
  });

  button.setCallback([](int state) {
    machinestate = BUTTON_PRESSED;
    button_count++;
  });

  node.addHandler(&ota);
  node.addHandler(&machinestate);

  Log.addPrintStream(std::make_shared<MqttLogStream>(mqttlogStream));

  auto t = std::make_shared<TelnetSerialStream>(telnetSerialStream);
  Log.addPrintStream(t);
  Debug.addPrintStream(t);

  // node.set_debug(true);
  // node.set_debugAlive(true);
  node.begin(BOARD_OLIMEX); // OLIMEX

  Log.println("Booted: " __FILE__ " " __DATE__ " " __TIME__ );
}

void loop() {
  node.loop();
  if (machinestate.state() == ACTIVE) {
    // Do something - like keep a relay powered
  }
  else if (machinestate.state() == BORED) {
    // nothing to do ... just waiting for a button press.
  }
  else if (machinestate.state() == BUTTON_PRESSED) {
    Log.println("Someone pressed the button - activate the relay in 1 second");
    // We do not need to do:
    //    machine.setState(ACTIVE);
    // here explicitly. Because the timeout on the BUTTON_PRESSED state goes
    // automaticaly to ACTIVE after 1 second.
  }
  else {
    // keep that relay off or something.
  }
}

