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
#include <ButtonDebounce.h>         // https://github.com/dirkx/ButtonDebounce.git
#include <OptoDebounce.h>           // https://github.com/dirkx/OptoDebounce.git

#define MACHINE             "lintzaag"
#define OFF_BUTTON          (SW2_BUTTON)
#define MAX_IDLE_TIME       (35 * 60 * 1000) // auto power off after 35 minutes of no use.

// Current reading whule runing 0.015 or higher
// Current reading while idling 0.005
// Current reading while off    0.020
#define CURRENT_THRESHHOLD  (0.005)

//#define OTA_PASSWD          "SomethingSecrit"

CurrentTransformer currentSensor = CurrentTransformer(CURRENT_GPIO);

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

ButtonDebounce button1(SW1_BUTTON, 150 /* mSeconds */);
ButtonDebounce button2(SW2_BUTTON, 150 /* mSeconds */);
OptoDebounce opto1 = OptoDebounce(OPTO1);

// Various logging options (in addition to Serial).
SyslogStream syslogStream = SyslogStream();
MqttLogStream mqttlogStream = MqttLogStream();
// TelnetSerialStream telnetSerialStream = TelnetSerialStream();


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
  { "No network",           LED::LED_FLASH,         NEVER       , NOCONN },           // should we reboot at some point ?
  { "Waiting for card",     LED::LED_IDLE,          NEVER       , WAITINGFORCARD },
  { "Checking card",        LED::LED_PENDING,           5 * 1000, WAITINGFORCARD },
  { "Rejecting noise/card", LED::LED_ERROR,             5 * 1000, WAITINGFORCARD },
  { "Powered - but idle",   LED::LED_ON,            NEVER       , WAITINGFORCARD },   // we leave poweroff idle to the code below.
  { "Running",              LED::LED_ON,            NEVER       , WAITINGFORCARD },
};

unsigned long laststatechange = 0;
static machinestates_t laststate = BOOTING;
machinestates_t machinestate = BOOTING;

unsigned long powered_total = 0, powered_last;
unsigned long running_total = 0, running_last;
unsigned long bad_poweroff = 0;
unsigned long idle_poweroff = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n\n");
  Serial.println("Booted: " __FILE__ " " __DATE__ " " __TIME__ );

  // Init the hardware and get it into a safe state.
  //
  pinMode(RELAY_GPIO, OUTPUT);
  digitalWrite(RELAY_GPIO, 0);

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
    machinestate = POWERED;
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
    if (machinestate != RUNNING) {
      machinestate = RUNNING;
      Log.println("Motor started");
    };

    if (machinestate < POWERED) {
      static unsigned long last = 0;
      if (millis() - last > 1000)
        Log.println("Very strange - current observed while we are 'off'. Should not happen.");
    }
  });

  currentSensor.onCurrentOff([](void) {
    // We let the auto-power off on timeout do its work.
    if (machinestate > POWERED) {
      machinestate = POWERED;
      Log.println("Motor stopped");
    };

  });

  button1.setCallback([](int state) {
    Debug.printf("Button 1 changed to %d\n", state);
  });
  button2.setCallback([](int state) {
    Debug.printf("Button 2 changed to %d\n", state);
  });

  node.onReport([](JsonObject  & report) {
    report["state"] = state[machinestate].label;

    report["powered_time"] = powered_total + ((machinestate == POWERED) ? ((millis() - powered_last) / 1000) : 0);
    report["running_time"] = running_total + ((machinestate == RUNNING) ? ((millis() - running_last) / 1000) : 0);

    report["idle_poweroff"] = idle_poweroff;
    report["bad_poweroff"] = bad_poweroff;

    report["current"] = currentSensor.sd();
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
  // default syslog port and destination (gateway address or broadcast address).
  //
#ifdef SYSLOG_HOST
  syslogStream.setDestination(SYSLOG_HOST);
  syslogStream.setRaw(true);
#ifdef SYSLOG_PORT
  syslogStream.setPort(SYSLOG_PORT);
#endif
#endif

  // General normal log goes to MQTT and Syslog (UDP).
  Log.addPrintStream(std::make_shared<MqttLogStream>(mqttlogStream));
  Log.addPrintStream(std::make_shared<SyslogStream>(syslogStream));

  // We only sent the very low level debugging to syslog.
  Debug.addPrintStream(std::make_shared<SyslogStream>(syslogStream));

#if 0
  // As the PoE devices have their own grounding - the cannot readily be connected
  // to with a sericd Peral cable.  This allows for a telnet instead.
  auto t = std::make_shared<TelnetSerialStream>(telnetSerialStream);

  Log.addPrintStream(t);
  Debug.addPrintStream(t);
#endif

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
  opto1.loop();
  currentSensor.loop();

  button1.update();
  button2.update();

  static bool haveSeenPower = false;

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

  if (button2.state() == LOW && machinestate >= POWERED) {
    if (machinestate == RUNNING) {
      Log.printf("Machine switched off with button while running (bad!)\n");
      bad_poweroff++;
    } else if (machinestate == POWERED) {
      Log.printf("Machine switched OFF with the off-button.\n");;
    } else {
      Log.printf("Off button pressed (currently in state %s). Weird.\n",
                 state[machinestate].label);
    }
    machinestate = WAITINGFORCARD;
  };

  if ((machinestate > WAITINGFORCARD) && (!opto1.state())) {
    // Once you have swiped your card - you have 120 seconds to hit the green button on the back.
    if (millis() - laststatechange > 120 * 1000) {
      Log.print("Switching off - card swiped but the green button was not pressed within 120 seconds.\n");
      machinestate = WAITINGFORCARD;
    }
    // If you have ran the machine - and press the red button on the back - go off pretty much right away.
    if (haveSeenPower && millis() - laststatechange > 1000) {
      Log.print("Switching off - red button at the back pressed.\n");
      machinestate = WAITINGFORCARD;
    }
  }

  if (opto1.state()) {
    if ((!haveSeenPower) && machinestate > WAITINGFORCARD)
      Log.print("Switched on - green button at the back pressed.\n");

    haveSeenPower = true;
  }

  if (machinestate <= WAITINGFORCARD)
    haveSeenPower = false;

  if (laststate < POWERED)
    digitalWrite(RELAY_GPIO, 0);
  else
    digitalWrite(RELAY_GPIO, 1);

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
