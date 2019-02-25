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
#define AART_LED           (04) // Large LED on middle front.
#define OPTO1              (35) // Two diode PC417 that checks if there is AC.
#define OPTO2              (33) // Two diode PC417 that checks if there is AC.
#define OPTO3              (32) // Two diode PC417 that checks if there is AC.

#include <ACNode.h>
#include <OptoDebounce.h>           // https://github.com/dirkx/OptocouplerDebouncer.git

#define MACHINE             "lights"

OptoDebounce opto1(OPTO1);
OptoDebounce opto2(OPTO2);
OptoDebounce opto3(OPTO3);

LED aartLed = LED(AART_LED, true); // LED is inverted.

ACNode node = ACNode(MACHINE); // PoE Wired, Olimex baord.

#ifdef OTA_PASSWD
OTA ota = OTA(OTA_PASSWD);
#endif

// Various logging options (in addition to Serial).
MqttLogStream mqttlogStream = MqttLogStream();


typedef enum {
  BOOTING, OUTOFORDER,      // device not functional.
  REBOOT,                   // forcefull reboot
  TRANSIENTERROR,           // hopefully goes away level error
  NOCONN,                   // sort of fairly hopless (though we can cache RFIDs!)
  POWERED,                  // unit is powered on
  RUNNING,                  // unit is running (opto sees light).
} machinestates_t;

#define NEVER (0)

struct {
  const char * label;                   // name of this state
  LED::led_state_t ledState;            // flashing pattern for the aartLED. Zie ook https://wiki.makerspaceleiden.nl/mediawiki/index.php/Powernode_1.1.
  time_t maxTimeInMilliSeconds;         // how long we can stay in this state before we timeout.
  machinestates_t failStateOnTimeout;   // what state we transition to on timeout.
} state[RUNNING + 1] =
{
  { "Booting",              LED::LED_ERROR,           120 * 1000, REBOOT },
  { "Out of order",         LED::LED_ERROR,           120 * 1000, REBOOT },
  { "Rebooting",            LED::LED_ERROR,           120 * 1000, REBOOT },
  { "Transient Error",      LED::LED_ERROR,           120 * 1000, REBOOT },
  { "No network",           LED::LED_FLASH,           120 * 1000, REBOOT },
  { "Powered - no lights",  LED::LED_IDLE,                 NEVER, POWERED },
  { "Lights are ON",        LED::LED_ON,                   NEVER, RUNNING },
};

unsigned long laststatechange = 0;
static machinestates_t laststate = BOOTING;
machinestates_t machinestate = BOOTING;

unsigned long powered_total = 0, powered_last;
unsigned long running_total = 0, running_last;

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n\n");
  Serial.println("Booted: " __FILE__ " " __DATE__ " " __TIME__ );

  aartLed.set(LED::LED_ERROR);

  // the default is space.makerspaceleiden.nl, prefix test
  // node.set_mqtt_host("mymqtt-server.athome.nl");
  // node.set_mqtt_prefix("test-1234");

  node.set_mqtt_prefix("ac");
  node.set_master("master");

  // specify this when using your own `master'.
  //
  // node.set_master("test-master");

  // node.set_report_period(2 * 1000);

  node.onConnect([]() {
    machinestate = POWERED;
  });
  node.onDisconnect([]() {
    machinestate = NOCONN;
  });
  node.onError([](acnode_error_t err) {
    Log.printf("Error %d\n", err);
    machinestate = TRANSIENTERROR;
  });

  node.onReport([](JsonObject  & report) {
    report["state"] = state[machinestate].label;

    report["powered_time"] = powered_total + ((machinestate == POWERED) ? ((millis() - powered_last) / 1000) : 0);
    report["running_time"] = running_total + ((machinestate == RUNNING) ? ((millis() - running_last) / 1000) : 0);

#ifdef OTA_PASSWD
    report["ota"] = true;
#else
    report["ota"] = false;
#endif
    report["acstate1"] = opto1.state();
    report["acstate2"] = opto2.state();
    report["acstate3"] = opto3.state();
  });

  Log.addPrintStream(std::make_shared<MqttLogStream>(mqttlogStream));
#ifdef OTA_PASSWD
  node.addHandler(&ota);
#endif

  // node.set_debug(true);
  // node.set_debugAlive(true);
  node.begin(BOARD_OLIMEX);
  Log.println("Booted: " __FILE__ " " __DATE__ " " __TIME__ );
}

void loop() {
  node.loop();
  opto1.loop();
  opto2.loop();
  opto3.loop();

  if (laststate != machinestate) {
    Log.printf("Changed from state <%s> to state <%s>\n",
               state[laststate].label, state[machinestate].label);

    if (machinestate == POWERED && laststate < POWERED) {
      powered_last = millis();
    } else if (laststate == POWERED && machinestate < POWERED) {
      powered_total += (millis() - running_last) / 1000;
    };
    if (machinestate == RUNNING && laststate < RUNNING) {
      running_last = millis();
    } else if (laststate == RUNNING && machinestate < RUNNING) {
      running_total += (millis() - running_last) / 1000;
    };
    laststate = machinestate;
    laststatechange = millis();
  }

  if (state[machinestate].maxTimeInMilliSeconds != NEVER &&
      (millis() - laststatechange > state[machinestate].maxTimeInMilliSeconds)) {
    laststate = machinestate;
    machinestate = state[machinestate].failStateOnTimeout;
    Debug.printf("Time-out; transition from %s to %s\n",
                 state[laststate].label, state[machinestate].label);
  };

  aartLed.set(state[machinestate].ledState);

  // This is a bit odd - you'd expect them to be identical. But it is not.
  // So we must have miswired something. But what ?!
  //
  // "acstate1":true,"acstate2":false,"acstate3":false}
  // "acstate1":false,"acstate2":true,"acstate3":true}
  if (opto1.state() != false || opto2.state() != true  || opto3.state() != true)
    machinestate = RUNNING;
  else
    machinestate = POWERED;

  switch (machinestate) {
    case REBOOT:
      node.delayedReboot();
      break;

    case POWERED:
      // Normal state -- PoE power is always on.
      break;
    case RUNNING:
      {
        static unsigned long last = 0;
        if (millis() - last > 60 * 1000 || last == 0) {
          Log.printf("Lights are on.\n");
          last = millis();
        };
      }
      break;

    case TRANSIENTERROR:
    case OUTOFORDER:
    case NOCONN:
    case BOOTING:
      break;
  };
}
