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
#include <ButtonDebounce.h>

#define MACHINE             "test-extract" /* "my name" -- default is 'node-<macaddres>' */
#define OFF_BUTTON          (SW2_BUTTON)
#define MAX_IDLE_TIME       (30 * 60 * 1000) // auto power off after 30 minutes of no demand.

//#define OTA_PASSWD          "SomethingSecrit"

// ACNode node = ACNode(MACHINE, WIFI_NETWORK, WIFI_PASSWD); // wireless, fixed wifi network.
// ACNode node = ACNode(MACHINE, false); // wireless; captive portal for configure.
// ACNode node = ACNode(MACHINE, true); // wired network (default).
ACNode node = ACNode(MACHINE);

#ifdef OTA_PASSWD
OTA ota = OTA(OTA_PASSWD);
#endif

LED aartLed = LED();    // defaults to the aartLed - otherwise specify a GPIO.

ButtonDebounce offButton(OFF_BUTTON, 150 /* mSeconds */);

MqttLogStream mqttlogStream = MqttLogStream();

typedef enum {
  BOOTING, OUTOFORDER,      // device not functional.
  REBOOT,                   // forcefull reboot
  TRANSIENTERROR,           // hopefully goes away level error
  NOCONN,                   // sort of fairly hopless (though we can cache RFIDs!)
  WAITING,                  // waiting for a request.
  RUNNING,                  // this is when the fan is on.
} machinestates_t;

#define NEVER (0)

struct {
  const char * label;                   // name of this state
  LED::led_state_t ledState;            // flashing pattern for the aartLED. Zie ook https://wiki.makerspaceleiden.nl/mediawiki/index.php/Powernode_1.1.
  time_t maxTimeInMilliSeconds;         // how long we can stay in this state before we timeout.
  machinestates_t failStateOnTimeout;   // what state we transition to on timeout.
} state[RUNNING + 1] =
{
  { "Booting",              LED::LED_ERROR,           120 * 1000, REBOOT  },
  { "Out of order",         LED::LED_ERROR,           120 * 1000, REBOOT  },
  { "Rebooting",            LED::LED_ERROR,           120 * 1000, REBOOT  },
  { "Transient Error",      LED::LED_ERROR,             5 * 1000, WAITING },
  { "No network",           LED::LED_FLASH,         15 * 60 * 1000, REBOOT  }, // is this a good idea ?
  { "Waiting",              LED::LED_IDLE,          NEVER       , WAITING },
  { "Running",              LED::LED_ON,    MAX_IDLE_TIME       , WAITING },
};

unsigned long laststatechange = 0;
static machinestates_t laststate = BOOTING;
machinestates_t machinestate = BOOTING;

unsigned long running_total = 0, running_last;
unsigned long button_poweroff = 0;
unsigned long idle_poweroff = 0;
unsigned long net_poweroff = 0;
unsigned long net_poweron = 0;
unsigned long net_renew = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n\n");
  Serial.println("Booted: " __FILE__ " " __DATE__ " " __TIME__ );

  pinMode(RELAY_GPIO, OUTPUT);
  digitalWrite(RELAY_GPIO, 1);

  pinMode(SW1_BUTTON, INPUT_PULLUP);
  pinMode(SW2_BUTTON, INPUT_PULLUP);

  Serial.printf("Boot state: SW1:%d SW2:%d\n",
                digitalRead(SW1_BUTTON), digitalRead(SW2_BUTTON));

  // the default is space.makerspaceleiden.nl, prefix test
  // node.set_mqtt_host("mymqtt-server.athome.nl");
  // node.set_mqtt_prefix("test-1234");

  // specify this when using your own `master'.
  //
  node.set_master("test-master");

  node.set_report_period(10 * 1000);

  node.onConnect([]() {
    machinestate = WAITING;
  });
  node.onDisconnect([]() {
    machinestate = NOCONN;
  });
  node.onError([](acnode_error_t err) {
    Log.printf("Error %d\n", err);
    machinestate = TRANSIENTERROR;
  });
  
//  node.onValidatedCmd([]<ACNode::cmd_result_t>(const char *cmd, const char * rest) {
  node.onValidatedCmd([](const char *cmd, const char * rest) {
    if (!strcasecmp(cmd, "stop")) {
      machinestate = WAITING;
      net_poweroff++;
      return ACNode::CMD_CLAIMED;
    };

    if (!strcasecmp(cmd, "run")) {
      if (machinestate != RUNNING) {
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
    report["state"] = state[machinestate].label;

    report["running_time"] = running_total + ((machinestate == RUNNING) ? ((millis() - running_last) / 1000) : 0);

    report["idle_poweroff"] = idle_poweroff;
    report["button_poweroff"] = button_poweroff;

    report["net_poweroff"] = net_poweroff;
    report["net_poweron"] = net_poweron;
    report["net_renew"] = net_renew;

#ifdef OTA_PASSWD
    report["ota"] = true;
#else
    report["ota"] = false;
#endif
  });

  // General normal log goes to MQTT and Syslog (UDP).
  Log.addPrintStream(std::make_shared<MqttLogStream>(mqttlogStream));

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
  offButton.update();

  if (state[machinestate].maxTimeInMilliSeconds != NEVER &&
      (millis() - laststatechange > state[machinestate].maxTimeInMilliSeconds)) {
    laststate = machinestate;
    machinestate = state[machinestate].failStateOnTimeout;
    Debug.printf("Time-out; too long in state %s; swiching to %s\n",
                 state[laststate].label, state[machinestate].label);
  };

  if (laststate != machinestate) {
    Debug.printf("Changed from state <%s> to state <%s>\n",
                 state[laststate].label, state[machinestate].label);

    if (machinestate == RUNNING && laststate < RUNNING) {
      running_last = millis();
    } else if (laststate == RUNNING && machinestate < RUNNING) {
      running_total += (millis() - running_last) / 1000;
    };
    laststate = machinestate;
    laststatechange = millis();
  }

  if (offButton.state() == LOW && machinestate >= RUNNING) {
    if (machinestate == RUNNING) {
      Log.printf("Machine switched OFF with the off-button.\n");
      button_poweroff++;
    } else {
      Log.printf("Off button pressed (currently in state %s). Weird.\n",
                 state[machinestate].label);
    }
    machinestate = WAITING;
  }

  if (laststate < RUNNING)
    digitalWrite(RELAY_GPIO, 0);
  else
    digitalWrite(RELAY_GPIO, 1);

  aartLed.set(state[machinestate].ledState);

  switch (machinestate) {
    case REBOOT:
      {
        static int warn_counter = 0;
        static unsigned long last = 0;
        if (millis() - last > 1000) {
          Log.println("Forced reboot.");
          Serial.println("Forced reboot");
          last = millis();
          warn_counter ++;
        };
        if (warn_counter > 5)
          Serial.println("Forced reboot NOW");
        ESP.restart();
      }
      break;

    case WAITING:
      break;

    case RUNNING:
      break;

    case TRANSIENTERROR:
    case OUTOFORDER:
    case NOCONN:
    case BOOTING:
      break;
  };
}
