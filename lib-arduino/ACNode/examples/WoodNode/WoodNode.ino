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
/* PoE (aart/lucas) based board near the wood workbench.

    Monitors current and witches on extraction & lets it run a bit longer.
    Listens for tablesaw 'extraction needed' messages (it has its own run-longer mngt).

    Monitors if the light is on  - and reports this.

    Currently NO rfid swipe support (all machines are free to use/under simple instructions).

*/

#include <PowerNodeV11.h>
#include <ACNode.h>
#include <CurrentTransformer.h>     // https://github.com/dirkx/CurrentTransformer

#define MACHINE             "wood"

ACNode node = ACNode(MACHINE);
LED aartLed = LED();
CurrentTransformer currentSensor = CurrentTransformer(CURRENT_GPIO, 197); //SVP, 197 Hz sampling of a 50hz signal/

MqttLogStream mqttlogStream = MqttLogStream();
TelnetSerialStream telnetSerialStream = TelnetSerialStream();

#ifdef OTA_PASSWD
OTA ota = OTA(OTA_PASSWD);
#endif

typedef enum {
  BOOTING, OUTOFORDER,      // device not functional.
  REBOOT,                   // forcefull reboot
  TRANSIENTERROR,           // hopefully goes away level error
  NOCONN,                   // sort of fairly hopless (though we can cache RFIDs!)
  ALL_OFF,                   // doing nothing.
  LIGHTS_ON,
  RUNNING,
  POSTRUN,
} machinestates_t;

#define NEVER (0)

struct {
  const char * label;                   // name of this state
  LED::led_state_t ledState;            // flashing pattern for the aartLED. Zie ook https://wiki.makerspaceleiden.nl/mediawiki/index.php/Powernode_1.1.
  time_t maxTimeInMilliSeconds;         // how long we can stay in this state before we timeout.
  machinestates_t failStateOnTimeout;   // what state we transition to on timeout.
  unsigned long timeInState;
  unsigned long timeoutTransitions;
  unsigned long autoReportCycle;
} state[POSTRUN + 1] =
{
  { "Booting",              LED::LED_ERROR,           120 * 1000,   REBOOT,             0 },
  { "Out of order",         LED::LED_ERROR,           120 * 1000,   REBOOT, 5 * 60 * 1000 },
  { "Rebooting",            LED::LED_ERROR,           120 * 1000,   REBOOT,             0 },
  { "Transient Error",      LED::LED_ERROR,             5 * 1000,  ALL_OFF, 5 * 60 * 1000 },
  { "No network",           LED::LED_FLASH,                NEVER,   NOCONN,             0 },
  { "Idle",                 LED::LED_IDLE,                 NEVER,  ALL_OFF,             0 },
  { "Lights On",            LED::LED_IDLE,                 NEVER,  ALL_OFF, 5 * 60 * 1000 },
  { "Extractor on",         LED::LED_IDLE,                 NEVER,  ALL_OFF, 1 * 60 * 1000 },
  { "Extractor on (post)",  LED::LED_IDLE,                 NEVER,  ALL_OFF, 1 * 60 * 1000 },
};


unsigned long laststatechange = 0, lastReport = 0, swipeouts_count = 0;
static machinestates_t laststate = OUTOFORDER;
machinestates_t machinestate = BOOTING;

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n\n");
  Serial.println("Booted: " __FILE__ " " __DATE__ " " __TIME__ );

  aartLed.set(LED::LED_FAST);
  currentSensor.setOnLimit(0.00125);

  node.set_mqtt_prefix("ac");
  node.set_master("master");

  node.onConnect([]() {
    Log.println("Connected");
    machinestate = WAITINGFORCARD;
  });
  node.onDisconnect([]() {
    Log.println("Disconnected");
    machinestate = NOCONN;

  });
  node.onError([](acnode_error_t err) {
    Log.printf("Error %d\n", err);
    machinestate = WAITINGFORCARD;
  });
  node.onReport([](JsonObject  & report) {
    report["state"] = state[machinestate].label;
  });
  currentSensor.onCurrentOn([](void) {
    if (machinestate < RUNNING) {
      machinestate = RUNNING;
      Log.println("Succobus started");
    };
  });
  currentSensor.onCurrentOff([](void) {
    // We let the auto-power off on timeout do its work.
    if (machinestate == RUNNING) {
      machinestate = POSTRUNNING;
      Log.println("Succobus into postrun");
    };
  });


#ifdef OTA_PASSWD
  report["ota"] = true;
#else
  report["ota"] = false;
#endif
});

node.addHandler(&reader);
#ifdef OTA_PASSWD
node.addHandler(&ota);
#endif

Log.addPrintStream(std::make_shared<MqttLogStream>(mqttlogStream));

auto t = std::make_shared<TelnetSerialStream>(telnetSerialStream);
Log.addPrintStream(t);
Debug.addPrintStream(t);

// node.set_debug(true);
// node.set_debugAlive(true);
node.begin();

Log.println("Booted: " __FILE__ " " __DATE__ " " __TIME__ );
}

void loop() {
  node.loop();
  oled.loop();

  if (laststate != machinestate) {
    Debug.printf("Changed from state <%s> to state <%s>\n",
                 state[laststate].label, state[machinestate].label);

    state[laststate].timeInState += (millis() - laststatechange) / 1000;
    laststate = machinestate;
    laststatechange = millis();
    oled.setText(state[machinestate].label);
    return;
  }

  if (state[machinestate].maxTimeInMilliSeconds != NEVER &&
      (millis() - laststatechange > state[machinestate].maxTimeInMilliSeconds))
  {
    state[machinestate].timeoutTransitions++;

    laststate = machinestate;
    machinestate = state[machinestate].failStateOnTimeout;

    Log.printf("Time-out; transition from <%s> to <%s>\n",
               state[laststate].label, state[machinestate].label);
    return;
  };

  if (state[machinestate].autoReportCycle && \
      millis() - laststatechange > state[machinestate].autoReportCycle && \
      millis() - lastReport > state[machinestate].autoReportCycle)
  {
    Log.printf("State: %s now for %lu seconds", state[laststate].label, (millis() - laststatechange) / 1000);
    lastReport = millis();
  };

  switch (machinestate) {
    case BOOTING:
    case OUTOFORDER:
    case TRANSIENTERROR:
    case NOCONN:
      break;

    case REBOOT:
      node.delayedReboot();
      break;

    case WAITINGFORCARD:
    case CHECKINGCARD:
    case REJECTED:
      // all handled in above stage engine.
      break;
  };
}

