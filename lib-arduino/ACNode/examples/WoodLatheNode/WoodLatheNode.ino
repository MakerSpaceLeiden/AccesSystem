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
#include <PurpleNodev107.h>
#include <TimerEvent.h>

#ifndef MACHINE
#define MACHINE   "woodlathe"
#endif

#define MAX_IDLE_TIME       (35 * 60 * 1000) // auto power off after 35 minutes of no use.
// . #define INTERLOCK      (OPTO2)
#define OFF_BUTTON          (BUTT0)
#define RELAY_GPIO          (OUT0)

//#define OTA_PASSWD          "SomethingSecrit"
PurpleNodev107 node = PurpleNodev107(MACHINE);


#ifdef OTA_PASSWD
OTA ota = OTA(OTA_PASSWD);
#endif

LED aartLed = LED(LED_INDICATOR);

TimerEvent monitorCurrent;


typedef enum {
  BOOTING, OUTOFORDER,      // device not functional.
  REBOOT,                   // forcefull reboot
  TRANSIENTERROR,           // hopefully goes away level error
  NOCONN,                   // sort of fairly hopless (though we can cache RFIDs!)
  WAITINGFORCARD,           // waiting for card.
  CHECKINGCARD,
  REJECTED,
  POWERED,                  // this is where we engage the relay.
  RUNNING,                  // this is when we detect a current.
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
  { "Transient Error",      LED::LED_ERROR,             5 * 1000, WAITINGFORCARD },
  { "No network",           LED::LED_FLASH,         NEVER     , NOCONN },           // should we reboot at some point ?
  { "Waiting for card",     LED::LED_IDLE,          NEVER     , WAITINGFORCARD },
  { "Checking card",        LED::LED_PENDING,           5 * 1000, WAITINGFORCARD },
  { "Rejecting noise/card", LED::LED_ERROR,             5 * 1000, WAITINGFORCARD },
  { "Powered - but idle",   LED::LED_ON,            NEVER     , WAITINGFORCARD },   // we leave poweroff idle to the code below.
  { "Running",              LED::LED_ON,            NEVER     , WAITINGFORCARD },
};

unsigned long laststatechange = 0;
static machinestates_t laststate = BOOTING;
machinestates_t machinestate = BOOTING;

unsigned long powered_total = 0, powered_last;
unsigned long running_total = 0, running_last;
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

  node.set_mqtt_prefix("ac");
  node.set_master("master");

  node.onConnect([]() {
    machinestate = WAITINGFORCARD;
  });
  node.onDisconnect([]() {
    machinestate = NOCONN;
  });
  node.onError([](acnode_error_t err) {
    Log.printf("Error %d\n", err);
    machinestate = TRANSIENTERROR;
    errors++;
  });
  node.onApproval([](const char * machine) {
    machinestate = POWERED;
    digitalWrite(BUZZER, HIGH); delay(50); digitalWrite(BUZZER, LOW);
  });

  node.onDenied([](const char * machine) {
    machinestate = REJECTED;
    for(int i = 0; i < 10; i++) {
      digitalWrite(BUZZER, HIGH); delay(100); digitalWrite(BUZZER, LOW);delay(100);
    };
  });

  node.onSwipe([](const char * tag) -> ACBase::cmd_result_t  {
  node.onReport([](JsonObject  & report) {
    report["state"] = state[machinestate].label;
#ifdef INTERLOCK
    report["interlock"] = digitalRead(INTERLOCK) ? true : false;
#endif
    report["powered_time"] = powered_total + ((machinestate == POWERED) ? ((millis() - powered_last) / 1000) : 0);
    report["running_time"] = running_total + ((machinestate == RUNNING) ? ((millis() - running_last) / 1000) : 0);

    report["idle_poweroff"] = idle_poweroff;
    report["bad_poweroff"] = bad_poweroff;
    report["manual_poweroff"] = manual_poweroff;

    report["current"] = analogRead(CURR0); // not very meaningful anymore with the RC circuit ?

    report["errors"] = errors;
  });

#ifdef OTA_PASSWD
  node.addHandler(&ota);
#endif

  node.begin();
  Log.println("Booted: " __FILE__ " " __DATE__ " " __TIME__ );
}

void loop() {
  node.loop();
  currentSensor.loop();
  monitorCurrent.update();

#ifdef INTERLOCK
  if (digitalRead(INTERLOCK)) {
    if (machinestate != OUTOFORDER || millis() - laststatechange > 60 * 1000) {
      Log.printf("Problem with the interlock -- is the big green connector unseated ?\n");
    }
    machinestate = OUTOFORDER;
  };
#endif

#ifdef OFF_BUTTON
  if (digitalRead(OFF_BUTTON) == LOW) {
    if (machinestate < POWERED)
      return; // button not yet function.
    if (machinestate > POWERED) {
      // Refuse to let the safety be used to power something off.
      digitalWrite(BUZZER, HIGH); delay(500); digitalWrite(BUZZER, LOW);
      bad_poweroff++;
      return;
    }
    Log.println("Machine powered down");
    machinestate = WAITINGFORCARD;
    manual_poweroff++;
  };
#endif

  if (laststate != machinestate) {
    Debug.printf("Changed from state <%s> to state <%s>\n",
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

  digitalWrite(RELAY_GPIO, (laststate < POWERED) ? LOW : HIGH);
  aartLed.set(state[machinestate].ledState);

  switch (machinestate) {
    case WAITINGFORCARD:
      break;

    case REBOOT:
      node.delayedReboot();
      break;

    case CHECKINGCARD:
      break;

    case POWERED:
      if ((millis() - laststatechange) > MAX_IDLE_TIME) {
        Log.printf("Machine idle for too long - switching off.\n");
        machinestate = WAITINGFORCARD;
        idle_poweroff++;
      }
      break;

    case RUNNING:
      break;

    case REJECTED:
      break;

    case TRANSIENTERROR:
      break;
    case OUTOFORDER:
    case NOCONN:
    case BOOTING:
      break;
  };
}
