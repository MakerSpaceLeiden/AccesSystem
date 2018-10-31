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
// Wiring of Power Node v.1.1
//
#include <PowerNodeV11.h>

#define MACHINE "test-woodlate"
#define OFF_BUTTON      (SW1_BUTTON)
#define MAX_IDLE_TIME   (35 * 60 * 1000) // auto power off after 35 minutes of no use.


#include <ACNode.h>
#include <RFID.h>   // SPI version

// ACNode node = ACNode(MACHINE, WIFI_NETWORK, WIFI_PASSWD); // wireless, fixed wifi network.
// ACNode node = ACNode(MACHINE, false); // wireless; captive portal for configure.
// ACNode node = ACNode(MACHINE, true); // wired network (default).
//
ACNode node = ACNode(MACHINE);
RFID reader = RFID();

// we should move this inside ACNode.
OTA ota = OTA(OTA_PASSWD);
// MSL msl = MSL();    // protocol doors (private LAN)
// SIG1 sig1 = SIG1(); // protocol machines 20015 (HMAC)
SIG2 sig2 = SIG2();
Beat beat = Beat();     // Required by SIG1 and SIG2

LED aartLed = LED();    // defaults to the aartLed - otherwise specify GPIO.

#include <ACNode.h>
#include <RFID.h>   // SPI version


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
  LED::led_state_t ledState;            // flashing pattern for the aartLED
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

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n\n");
  Serial.println("Booted: " __FILE__ " " __DATE__ " " __TIME__ );

  // Init the hardware and get it into a safe state.
  //
  pinMode(RELAY_GPIO, OUTPUT);
  digitalWrite(RELAY_GPIO, 0);

  pinMode(CURRENT_GPIO, INPUT); // analog input.
  pinMode(OFF_BUTTON, INPUT_PULLUP);

  // the default is space.makerspaceleiden.nl, prefix test
  // node.set_mqtt_host("mymqtt-server.athome.nl");
  // node.set_mqtt_prefix("test-1234");

  // specify this when using your own `master'.
  //
  node.set_master("test-master");

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

  node.addHandler(&ota);

  // node.addSecurityHandler(&msl);
  // node.addSecurityHandler(&sig1);
  node.addSecurityHandler(&sig2);
  node.addSecurityHandler(&beat);

  // default syslog port and destination (gateway address or broadcast address).
  //
  SyslogStream syslogStream = SyslogStream();
#ifdef SYSLOG_HOST
  syslogStream.setDestination(SYSLOG_HOST);
  syslogStream.setRaw(true);
#ifdef SYSLOG_PORT
  syslogStream.setPort(SYSLOG_PORT);
#endif
#endif

  // Debug.addPrintStream(std::make_shared<SyslogStream>(syslogStream));
  Log.addPrintStream(std::make_shared<SyslogStream>(syslogStream));
  Debug.addPrintStream(std::make_shared<SyslogStream>(syslogStream));

  // assumes the client connection for MQTT (and network, etc) is up - otherwise silenty fails/buffers.
  //
  MqttLogStream mqttlogStream = MqttLogStream("test", "moi");
  Log.addPrintStream(std::make_shared<MqttLogStream>(mqttlogStream));

#if 0
  // As the PoE devices have their own grounding - the cannot readily be connected
  // to with a sericd Peral cable.  This allows for a telnet instead.
  TelnetSerialStream telnetSerialStream = TelnetSerialStream();
  auto t = std::make_shared<TelnetSerialStream>(telnetSerialStream);

  Log.addPrintStream(t);
  Debug.addPrintStream(t);
#endif

  // node.set_debug(true);
  // node.set_debugAlive(true);

  reader.onSwipe([](const char * tag) -> ACBase::cmd_result_t  {
    machinestate = CHECKINGCARD;

    // We'r declining so that the core library handle sending
    // an approval request, keep state, and so on.
    //
    return ACBase::CMD_DECLINE;
  });
  node.addHandler(&reader);

  node.begin();
  Debug.println("Booted: " __FILE__ " " __DATE__ " " __TIME__ );

  // secrit reset button.
  if (digitalRead(SW2_BUTTON) == LOW) {
    extern void wipe_eeprom();
    Log.println("Wiped EEPROM");
    wipe_eeprom();
  }
}

void loop() {
  node.loop();

  if (analogRead(CURRENT_GPIO) > 512) {
    if (machinestate < POWERED) {
      Log.printf("Error -- device in state '%s' but current detected!",
                 state[machinestate].label);
    }
    if (machinestate == POWERED) {
      machinestate = RUNNING;
      Log.printf("Machine running.");
    }
  } else if (machinestate == RUNNING) {
    machinestate = POWERED;
    Log.printf("Machine halted.");
  }

  // We do not worry much about button bounce; as it is just an off
  // we want to detect. But we'll go to some length to detect a
  // lengthy anomaly - as to not fill up the logs iwth one report/second.
  //
  if (digitalRead(OFF_BUTTON) == LOW && machinestate >= POWERED) {
    if (machinestate == RUNNING) {
      Log.printf("Machine switched off with button while running (bad!)\n");
    } else if (machinestate == POWERED) {
      Log.printf("Machine switched completely off with button.\n");;
    } else {
      Log.printf("Off button pressed (currently in state %s)\n",
                 state[machinestate].label);
    }
    machinestate = WAITINGFORCARD;
  };

  if (state[machinestate].maxTimeInMilliSeconds != NEVER &&
      (millis() - laststatechange > state[machinestate].maxTimeInMilliSeconds)) {
    machinestate = state[machinestate].failStateOnTimeout;
    Debug.printf("Time-out; transition from %s to %s\n",
                 state[laststatechange].label, state[machinestate].label);
  };

  if (laststate != machinestate) {
    Debug.printf("Changed from state <%s> to state <%s>\n",
                 state[laststate].label, state[machinestate].label);
    laststate = machinestate;
    laststatechange = millis();
  }

  if (laststate < POWERED)
    digitalWrite(RELAY_GPIO, 0);
  else
    digitalWrite(RELAY_GPIO, 1);

  aartLed.set(state[machinestate].ledState);

  switch (machinestate) {
    case WAITINGFORCARD:
      break;

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
          ESP.restart();
      }
      break;

    case CHECKINGCARD:
      break;

    case POWERED:
      if ((millis() - laststatechange) > MAX_IDLE_TIME) {
        Log.printf("Machine idle for too long - switching off.\n");
        machinestate = WAITINGFORCARD;
      }
      break;

    case RUNNING:
      break;

    case REJECTED:
      break;

    case TRANSIENTERROR:
      break;

    case NOCONN:
    case BOOTING:
    case OUTOFORDER:
      break;
  };
}

