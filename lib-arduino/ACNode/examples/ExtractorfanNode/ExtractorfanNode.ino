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
*/

// NodeMCU based.
//
#include <ACNode.h>
#include <ButtonDebounce.h>
#include "MachineState.h"

// See https://wiki.makerspaceleiden.nl/mediawiki/index.php/NodeAfzuiging

#define MACHINE             "NodeAfzuiging" /* "my name" -- default is 'node-<macaddres>' */

// Node MCU has a weird mapping...
//
#define LED_GREEN   16 // D0 -- LED inside the on/off toggle switch
#define LED_ORANGE  5  // D1 -- LED inside the orange, bottom, push button.
#define RELAY       4  // D2 -- relay (220V, 10A on just the L)
#define PUSHBUTTON  0  // D3 -- orange push button; 0=pressed, 1=released

#define RELAY_ENGAGED (LOW)

#define MAX_IDLE_TIME       (30 * 60 * 1000) // auto power off after 30 minutes of no demand.

// NodeMCU on WiFi -- see the wiki.
//
ACNode node = ACNode(MACHINE, WIFI_NETWORK, WIFI_PASSWD); // wireless, fixed wifi network.
// ACNode node = ACNode(MACHINE, false); // wireless; captive portal for configure.
// ACNode node = ACNode(MACHINE, true); // wired network (default).
// ACNode node = ACNode(MACHINE);

OTA ota = OTA(OTA_PASSWD);

LED aartLed = LED();    // defaults to the aartLed - otherwise specify a GPIO.

ButtonDebounce offButton(PUSHBUTTON, 150 /* mSeconds */);

TelnetSerialStream telnetSerialStream = TelnetSerialStream();
MqttLogStream mqttlogStream = MqttLogStream();

MachineState machinestate = MachineState();
MachineState::machinestates_t NOT_RUNNING, RUNNING;

// Counters
unsigned long button_poweroff = 0;
unsigned long button_poweron = 0;
unsigned long net_poweroff = 0;
unsigned long net_poweron = 0;
unsigned long net_renew = 0;

bool running = false;

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n\n");
  Serial.println("Booted: " __FILE__ " " __DATE__ " " __TIME__ );

  pinMode(RELAY_GPIO, OUTPUT);
  digitalWrite(RELAY_GPIO, ! RELAY_ENGAGED);

  // the default is space.makerspaceleiden.nl, prefix test
  // node.set_mqtt_host("mymqtt-server.athome.nl");
  // node.set_mqtt_prefix("test-1234");

  // specify this when using your own `master'.
  //
  node.set_master("master");

  NOT_RUNNING = machinestate.addState("Off (fan not runnning)", LED::LED_IDLE, 0, -1);
  RUNNING = machinestate.addState("Fan Running", LED::LED_ON, 0, -1);

  node.onConnect([]() {
    if (machinestate.state() < NOT_RUNNING)
      machinestate = NOT_RUNNING;
  });

  node.onDisconnect([]() {
    if (machinestate.state() < NOT_RUNNING)
      machinestate = NOT_RUNNING;
  });

  node.onError([](acnode_error_t err) {
    Log.printf("Error %d\n", err);
    machinestate = MachineState::TRANSIENTERROR;
  });

  node.onValidatedCmd([](const char *cmd, const char * rest) -> ACBase::cmd_result_t  {
    if (!strcasecmp(cmd, "stop")) {
      machinestate = NOT_RUNNING;
      running = false;
      net_poweroff++;
      return ACNode::CMD_CLAIMED;
    };

    if (!strcasecmp(cmd, "run")) {
      running = true;
      if (machinestate.state() != RUNNING) {
        net_poweron++;
        machinestate = RUNNING;
      } else {
        net_renew++;
      }
      return ACBase::CMD_CLAIMED;
    };
    return ACBase::CMD_DECLINE;
  });

  node.onReport([](JsonObject  & report) {
    report["button_poweron"] = button_poweron;
    report["button_poweroff"] = button_poweroff;

    report["net_poweroff"] = net_poweroff;
    report["net_poweron"] = net_poweron;
    report["net_renew"] = net_renew;

    report["running"] = running;
  });

  offButton.setCallback([](int btn) {
    if (btn == LOW)
      running = !running;
    if (running)
      button_poweron++;
    else
      button_poweroff++;
  });

  // General normal log goes to MQTT and Syslog (UDP).
  Log.addPrintStream(std::make_shared<MqttLogStream>(mqttlogStream));

  auto t = std::make_shared<TelnetSerialStream>(telnetSerialStream);
  Log.addPrintStream(t);
  Debug.addPrintStream(t);

  node.addHandler(&ota);

  // node.set_debug(true);
  // node.set_debugAlive(true);
  node.begin();
  Log.println("Booted: " __FILE__ " " __DATE__ " " __TIME__ );
}

void loop() {
  node.loop();
  offButton.update();

  digitalWrite(RELAY_GPIO, running ? RELAY_ENGAGED : !RELAY_ENGAGED);
}
