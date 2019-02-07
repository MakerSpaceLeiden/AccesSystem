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
#include <PowerNodeV11.h>
#include <ACNode.h>
#include <RFID.h>   // SPI version

#define MACHINE             "voordeur"

#define SOLENOID_GPIO     (4)
#define SOLENOID_OFF      (LOW)
#define SOLENOID_ENGAGED  (HIGH)

#define AARTLED_GPIO      (16)

#define BUZZ_TIME (5 * 1000) // Buzz 8 seconds.

ACNode node = ACNode(MACHINE);
RFID reader = RFID();
LED aartLed = LED();    // defaults to the aartLed - otherwise specify a GPIO.

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
  WAITINGFORCARD,           // waiting for card.
  CHECKINGCARD,
  REJECTED,
  BUZZING,                    // this is where we engage the solenoid.
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
} state[BUZZING + 1] =
{
  { "Booting",              LED::LED_ERROR,           120 * 1000, REBOOT,         0 },
  { "Out of order",         LED::LED_ERROR,           120 * 1000, REBOOT,         5 * 60 * 1000 },
  { "Rebooting",            LED::LED_ERROR,           120 * 1000, REBOOT,         0 },
  { "Transient Error",      LED::LED_ERROR,             5 * 1000, WAITINGFORCARD, 5 * 60 * 1000 },
  { "No network",           LED::LED_FLASH,                NEVER, NOCONN,         0 },
  { "Waiting for card",     LED::LED_IDLE,                 NEVER, WAITINGFORCARD, 0 },
  { "Checking card",        LED::LED_PENDING,           5 * 1000, WAITINGFORCARD, 0 },
  { "Rejected",             LED::LED_ERROR,             2 * 1000, WAITINGFORCARD, 0 },
  { "Buzzing door",         LED::LED_ON,               BUZZ_TIME, WAITINGFORCARD, 0 },
};


unsigned long laststatechange = 0, lastReport = 0;
static machinestates_t laststate = OUTOFORDER;
machinestates_t machinestate = BOOTING;

unsigned long opening_door_count  = 0, door_denied_count = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n\n");
  Serial.println("Booted: " __FILE__ " " __DATE__ " " __TIME__ );

  // Init the hardware and get it into a safe state.
  //
  pinMode(SOLENOID_GPIO, OUTPUT);
  digitalWrite(SOLENOID_GPIO, SOLENOID_OFF);

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
  node.onApproval([](const char * machine) {
    machinestate = BUZZING;
    opening_door_count++;
  });
  node.onDenied([](const char * machine) {
    machinestate = REJECTED;
    door_denied_count ++;
  });

  node.onReport([](JsonObject  & report) {
    report["state"] = state[machinestate].label;
    report["opening_door_count"] = opening_door_count;
    report["door_denied_count"] = door_denied_count;
    report["state"] = state[machinestate].label;
    report["opens"] = opening_door_count;

#ifdef OTA_PASSWD
    report["ota"] = true;
#else
    report["ota"] = false;
#endif
  });


  // This reports things such as FW version of the card; which can 'wedge' it. So we
  // disable it unless we absolutely positively need that information.
  //
  reader.set_debug(false);
  node.addHandler(&reader);
#ifdef OTA_PASSWD
  node.addHandler(&ota);
#endif

  Log.addPrintStream(std::make_shared<MqttLogStream>(mqttlogStream));

#if 1
  auto t = std::make_shared<TelnetSerialStream>(telnetSerialStream);
  Log.addPrintStream(t);
  Debug.addPrintStream(t);
#endif

  // node.set_debug(true);
  // node.set_debugAlive(true);
  node.begin();
  Log.println("Booted: " __FILE__ " " __DATE__ " " __TIME__ );
}

void loop() {
  node.loop();

  if (laststate != machinestate) {
    Debug.printf("Changed from state <%s> to state <%s>\n",
                 state[laststate].label, state[machinestate].label);

    state[laststate].timeInState += (millis() - laststatechange) / 1000;
    laststate = machinestate;
    laststatechange = millis();
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

  digitalWrite(SOLENOID_GPIO, (machinestate == BUZZING) ? SOLENOID_ENGAGED : SOLENOID_OFF);

  switch (machinestate) {
    case REBOOT:
      node.delayedReboot();
      break;

    case WAITINGFORCARD:
    case CHECKINGCARD:
    case REJECTED:
    case BUZZING:
      // all handled in above stage engine.
      break;

    case BOOTING:
    case OUTOFORDER:
    case TRANSIENTERROR:
    case NOCONN:
      break;
  };
}
