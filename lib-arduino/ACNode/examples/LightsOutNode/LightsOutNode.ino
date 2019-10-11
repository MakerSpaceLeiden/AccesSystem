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
// Olimex ESP32 base PoE
// https://www.olimex.com/Products/IoT/ESP32/ESP32-POE/open-source-hardware
// https://wiki.makerspaceleiden.nl/mediawiki/index.php/NodeLightsOut
//
#define AART_LED           ((gpio_num_t) 04) // Large LED on middle front.

#define MAINSSENSOR        ((gpio_num_t) 34) // 433Mhz receiver; see https://wiki.makerspaceleiden.nl/mediawiki/index.php/MainsSensor

#define OPTO1              ((gpio_num_t) 35) // Two diode PC817 that checks if there is AC. No capacitor/diode. Simple 100Hz.
#define OPTO2              ((gpio_num_t) 33) // Two diode PC817 that checks if there is AC.
#define OPTO3              ((gpio_num_t) 32) // Two diode PC817 that checks if there is AC.

#include <ACNode.h>
#include <OptoDebounce.h>           // https://github.com/dirkx/OptocouplerDebouncer.git
#include <mainsSensor.h>            // https://github.com/MakerSpaceLeiden/mainsSensor
#include <lwip/def.h> // for htons()

#define MACHINE             "lights"

OptoDebounce opto1(OPTO1);
OptoDebounce opto2(OPTO2);
OptoDebounce opto3(OPTO3);

LED aartLed = LED(AART_LED, true); // LED is inverted.

ACNode node = ACNode(MACHINE); // PoE Wired, Olimex baord.

OTA ota = OTA(OTA_PASSWD);

// Various logging options (in addition to Serial).
MqttLogStream mqttlogStream = MqttLogStream();
TelnetSerialStream telnetSerialStream = TelnetSerialStream();

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
unsigned long mains_datagrams_seen = 0;
unsigned long radio_bits_seen  = 0;

MainSensorReceiver msr = MainSensorReceiver(
                           MAINSSENSOR,
[](mainsnode_datagram_t * node) {
  switch (node->state) {
    case MAINSNODE_STATE_ON:
      Log.printf("Node %04x is on", node->id16);
      break;
    case MAINSNODE_STATE_OFF:
      Log.printf("Node %04x is OFF", node->id16);
      break;
    default:
      Log.printf("Node %04x sent a value I do not understand.", node->id16);
  };
  mains_datagrams_seen++;
});

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

    report["radio_cbs"] = radio_bits_seen;
    report["mains_datagrams"] = mains_datagrams_seen;
  });

  Log.addPrintStream(std::make_shared<MqttLogStream>(mqttlogStream));

  auto t = std::make_shared<TelnetSerialStream>(telnetSerialStream);
  Log.addPrintStream(t);
  Debug.addPrintStream(t);

#ifdef OTA_PASSWD
  node.addHandler(&ota);
#endif

  // node.set_debug(true);
  // node.set_debugAlive(true);
  node.begin(BOARD_OLIMEX);


  // No pullup - we're wired to a voltage divider to turn the 5v IO of the antenna unit to our 3v3.
  //
  pinMode(MAINSSENSOR, INPUT);
  msr.setup(200 /* 200 micro seconds for a half bit */);
  msr.setCallback([](mainsnode_datagram_t * node) {
    static int ok = 0;
    unsigned long secs = 1 + millis() / 1000;
    ok++;
    Log.printf("%06lu:%02lu:%02lu %4.1f%% :\t",
                  secs / 3600, (secs / 60) % 60, secs % 60,  ok * 100. / secs);

    switch (node->state) {
      case MAINSNODE_STATE_ON:
        Log.printf("mainsSensorNode %04x is on\n", htons(node->id16));
        break;
      case MAINSNODE_STATE_OFF:
        Log.printf("mainsSensorNode %04x is OFF\n", htons(node->id16));
        break;
      case MAINSNODE_STATE_DEAD:
        Log.printf("Node %04x has gone off air\n", htons(node->id16));
        break;
      default:
        Log.printf("mainsSensorNode %04x sent a value %x I do not understand.\n", htons(node->id16), node->state);
    }
  });
  msr.begin();
  Log.println("Booted: " __FILE__ " " __DATE__ " " __TIME__ );
}

void loop() {
  if (millis() > 10000) {
    static int lb = -1, x;
    int la = digitalRead(MAINSSENSOR);
    if (la != lb) {
      if (x++ < 100) Log.printf("%d\n", la ? 1 : 0); lb = la;
    };
  };

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

  // This is a bit odd - you'd expect them to be identical. But it is not. They are on
  // two different phases though - and one wire has an odd colour. Not investigated.
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
