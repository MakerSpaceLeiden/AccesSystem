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
// Wiring of Power Node v.1.1
//
#include <PowerNodeV11.h>
#include <ACNode.h>
#include <RFID.h>   // SPI version

#include <CurrentTransformer.h>     // https://github.com/dirkx/CurrentTransformer
#include <ButtonDebounce.h>         // https://github.com/craftmetrics/esp32-button
#include <OptoDebounce.h>

// Triac -- switches the DeWalt
// Relay -- switches the table saw itself.

#define MACHINE             "tablesaw"
#define MAX_IDLE_TIME       (35 * 60 * 1000) // auto power off after 35 minutes of no use.
#define EXTRACTION_EXTRA    (8 * 1000) // 20 seconds extra time on the fan.
#define CURRENT_THRESHHOLD  (0.002500)

//#define OTA_PASSWD          "SomethingSecrit"

CurrentTransformer currentSensor = CurrentTransformer(CURRENT_GPIO);
OptoDebounce opto2 = OptoDebounce(OPTO2, 500 /* mSecond */);

#include <ACNode.h>
#include <RFID.h>   // SPI version

// ACNode node = ACNode(MACHINE, WIFI_NETWORK, WIFI_PASSWD); // wireless, fixed wifi network.
// ACNode node = ACNode(MACHINE, false); // wireless; captive portal for configure.
// ACNode node = ACNode(MACHINE, true); // wired network (default).
ACNode node = ACNode(MACHINE);

// RFID reader = RFID(RFID_SELECT_PIN, RFID_RESET_PIN, -1, RFID_CLK_PIN, RFID_MISO_PIN, RFID_MOSI_PIN); //polling
// RFID reader = RFID(RFID_SELECT_PIN, RFID_RESET_PIN, RFID_IRQ_PIN, RFID_CLK_PIN, RFID_MISO_PIN, RFID_MOSI_PIN); //iRQ
RFID reader = RFID();

#ifdef OTA_PASSWD
OTA ota = OTA(OTA_PASSWD);
#endif

LED aartLed = LED(AART_LED);    // defaults to the aartLed - otherwise specify a GPIO.

MqttLogStream mqttlogStream = MqttLogStream();

typedef enum {
  BOOTING, OUTOFORDER,      // device not functional.
  REBOOT,                   // forcefull reboot
  TRANSIENTERROR,           // hopefully goes away level error
  NOCONN,                   // sort of fairly hopless (though we can cache RFIDs!)
  WAITINGFORCARD,           // waiting for card.
  CHECKINGCARD,
  REJECTED,
  ENABLED,                  // this is where we engage the relay.
  POWERED,                  // The user has pressed the green button on the safety relay.
  EXTRACTION,               // run the extractor fan a bit longer.
  RUNNING,                  // this is when we detect a current.
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
} state[RUNNING + 1] =
{
  { "Booting",              LED::LED_ERROR,           120 * 1000, REBOOT,         0 },
  { "Out of order",         LED::LED_ERROR,           120 * 1000, REBOOT,         5 * 60 * 1000 },
  { "Rebooting",            LED::LED_ERROR,           120 * 1000, REBOOT,         0 },
  { "Transient Error",      LED::LED_ERROR,             5 * 1000, WAITINGFORCARD, 5 * 60 * 1000 },
  { "No network",           LED::LED_FLASH,                NEVER, NOCONN,         0 },           // should we reboot at some point ?
  { "Waiting for card",     LED::LED_IDLE,                 NEVER, WAITINGFORCARD, 0 },
  { "Checking card",        LED::LED_PENDING,           5 * 1000, WAITINGFORCARD, 0 },
  { "Rejecting noise/card", LED::LED_ERROR,             5 * 1000, WAITINGFORCARD, 0 },
  { "Contactor Enabled",    LED::LED_ON,               30 * 1000, WAITINGFORCARD, 0},
  { "Powered - but idle",   LED::LED_ON,           MAX_IDLE_TIME, WAITINGFORCARD, 5 * 60 * 1000 },
  { "Powered + extracton",  LED::LED_ON,        EXTRACTION_EXTRA, POWERED,        1 * 60 * 1000 },
  { "Running",              LED::LED_ON,                   NEVER, WAITINGFORCARD, 1 * 60 * 1000 },
};

unsigned long laststatechange = 0, lastReport = 0;
static machinestates_t laststate = BOOTING;
machinestates_t machinestate = BOOTING;

unsigned long bad_poweroff = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n\n");
  Serial.println("Booted: " __FILE__ " " __DATE__ " " __TIME__ );

  // Init the hardware and get it into a safe state.
  //
  pinMode(RELAY_GPIO, OUTPUT);
  digitalWrite(RELAY_GPIO, 0);

  pinMode(TRIAC_GPIO, OUTPUT);
  digitalWrite(TRIAC_GPIO, 0);

  if (0) {
    for (int i = 0; i < 4; i++) {
      digitalWrite(TRIAC_GPIO, 1);
      digitalWrite(AART_LED, 1);
      delay(300);
      digitalWrite(TRIAC_GPIO, 0);
      digitalWrite(AART_LED, 0);
      delay(300);
    }
  }
  pinMode(CURRENT_GPIO, INPUT); // analog input.
  pinMode(SW1_BUTTON, INPUT_PULLUP);
  pinMode(SW2_BUTTON, INPUT_PULLUP);

  Serial.printf("Boot state: SW1:%d SW2:%d\n",
                digitalRead(SW1_BUTTON), digitalRead(SW2_BUTTON));

  // the default is space.makerspaceleiden.nl, prefix test
  // node.set_mqtt_host("mymqtt-server.athome.nl");
  // node.set_mqtt_prefix("test-1234");
  node.set_mqtt_prefix("ac");

  // specify this when using your own `master'.
  //
  node.set_master("master");
  // node.set_report_period(10 * 1000);

  node.onConnect([]() {
    machinestate = WAITINGFORCARD;
  });
  node.onDisconnect([]() {
    machinestate = NOCONN;
  });
  node.onError([](acnode_error_t err) {
    Log.printf("Error %d\n", err);
    machinestate = TRANSIENTERROR;
  });
  node.onApproval([](const char * machine) {
    machinestate = ENABLED;
  });
  node.onDenied([](const char * machine) {
    machinestate = REJECTED;
  });

  reader.onSwipe([](const char * tag) -> ACBase::cmd_result_t  {
    // avoid swithing off a machine unless we have to.
    //
    if (machinestate < POWERED)
      machinestate = CHECKINGCARD;

    // We'r declining so that the core library handle sending
    // an approval request, keep state, and so on.
    //
    return ACBase::CMD_DECLINE;
  });

  currentSensor.setOnLimit(CURRENT_THRESHHOLD);

  currentSensor.onCurrentOn([](void) {
    if (machinestate >=  POWERED) {
      machinestate = RUNNING;
      Log.println("Motor started");
    } else {
      static unsigned long last = 0;
      if (millis() - last > 1000)
        Log.println("Very strange - current observed while we are 'off'. Should not happen.");
    }
  });

  currentSensor.onCurrentOff([](void) {
    // We let the auto-power off on timeout do its work.
    if (machinestate > POWERED) {
      machinestate = EXTRACTION;
      Log.println("Motor stopped, extractor fan still on");
    };
  });

  node.onReport([](JsonObject  & report) {
    report["state"] = state[machinestate].label;

    report["powered_time"] = state[POWERED].timeInState + state[RUNNING].timeInState +  state[EXTRACTION].timeInState
                             + ((machinestate >= POWERED) ? ((millis() - laststatechange) / 1000) : 0);

    report["extract_time"] = state[RUNNING].timeInState +  state[EXTRACTION].timeInState
                             + ((machinestate >= EXTRACTION) ? ((millis() - laststatechange) / 1000) : 0);

    report["running_time"] = state[RUNNING].timeInState
                             + ((machinestate >= RUNNING) ? ((millis() - laststatechange) / 1000) : 0);

    report["idle_poweroff"] = state[POWERED].timeoutTransitions;
    report["fan_poweroff"] = state[EXTRACTION].timeoutTransitions;

    report["bad_poweroff"] = bad_poweroff;

    report["current"] = currentSensor.sd();

    report["opto"] = opto2.state() ? "high" : "low";

    report["triac"] = digitalRead(TRIAC_GPIO);
    report["relay"] = digitalRead(RELAY_GPIO);

#ifdef OTA_PASSWD
    report["ota"] = true;
#else
    report["ota"] = false;
#endif
  });

  // This reports things such as FW version of the card; which can 'wedge' it. So we
  // disable it unless we absolutely positively need that information.
  //
  reader.set_debug(true);

  node.addHandler(&reader);
  // default syslog port and destination (gateway address or broadcast address).
  //

  // General normal log goes to MQTT and Syslog (UDP).
  Log.addPrintStream(std::make_shared<MqttLogStream>(mqttlogStream));
  // Debug.addPrintStream(std::make_shared<MqttLogStream>(mqttlogStream));

#ifdef OTA_PASSWD
  node.addHandler(&ota);
#endif

  // node.set_debug(true);
  // node.set_debugAlive(true);
  node.begin();
  Log.println("Booted: " __FILE__ " " __DATE__ " " __TIME__ );
}

void loop() {
  node.loop();
  currentSensor.loop();
  opto2.loop();

  if (state[machinestate].maxTimeInMilliSeconds != NEVER &&
      (millis() - laststatechange > state[machinestate].maxTimeInMilliSeconds))
  {
    state[machinestate].timeoutTransitions++;

    laststate = machinestate;
    machinestate = state[machinestate].failStateOnTimeout;

    Log.printf("Time-out; transition from <%s> to <%s>\n",
               state[laststate].label, state[machinestate].label);
  };

  if (laststate != machinestate) {
    Debug.printf("Changed from state <%s> to state <%s>\n",
                 state[laststate].label, state[machinestate].label);

    state[laststate].timeInState += (millis() - laststatechange) / 1000;
    laststate = machinestate;
    laststatechange = millis();
  }
  if (state[machinestate].autoReportCycle && \
      millis() - laststatechange > state[machinestate].autoReportCycle && \
      millis() - lastReport > state[machinestate].autoReportCycle)
  {
    Log.printf("State: %s now for %lu seconds", state[laststate].label, (millis() - laststatechange) / 1000);
    lastReport = millis();
  };

  digitalWrite(RELAY_GPIO,  (laststate >= ENABLED) ? 1 : 0);
  digitalWrite(TRIAC_GPIO,  (laststate >= EXTRACTION) ? 1 : 0);

  aartLed.set(state[machinestate].ledState);

  switch (machinestate) {
    case WAITINGFORCARD:
      if (opto2.state()) {
        static unsigned long lastWarnig = 0;
        if (millis() - lastWarnig > 1 * 60 * 1000) {
          lastWarnig = millis();
          Log.printf("ODD: Control node is switched off - but voltage on motor detected\n");
        }
      }
      break;

    case REBOOT:
      node.delayedReboot();
      break;

    case CHECKINGCARD:
      break;

    case ENABLED:
      if (opto2.state()) {
        Log.printf("Machine switched ON with the safety contacto green on-button.\n");
        machinestate = POWERED;
      };
      break;

    case EXTRACTION:
    case POWERED: {
        static unsigned long last = 0;
        if (!opto2.state()) {
          if (millis() - last > 500) {
            Log.printf("Machine switched OFF with the safety contactor off-button.\n");
            machinestate = WAITINGFORCARD;
          } else {
            last = millis();
          }
        };
      };
      break;

    case RUNNING:
      {
        static unsigned long last = 0;
        if (!opto2.state()) {
          if (millis() - last > 500) {
            Log.printf("Machine switched OFF with safety contactorbutton while running (bad!)\n");
            bad_poweroff++;
            machinestate = WAITINGFORCARD;
          } else {
            last = millis();
          }
        };
      }
      break;

    case REJECTED:
      break;

    case TRANSIENTERROR:
    case OUTOFORDER:
    case NOCONN:
    case BOOTING:
      break;
  };
}
