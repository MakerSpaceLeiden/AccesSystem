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
// #include <PowerNodeV11.h> -- this is an olimex board.
#include <ACNode.h>
#include <RFID.h>
#include "OLED.h"

#define MACHINE             "byebye"


const uint8_t I2C_SDA_PIN = 13; //SDA;  // i2c SDA Pin, ext 2, pin 10
const uint8_t I2C_SCL_PIN = 16; //SCL;  // i2c SCL Pin, ext 2, pin 7

const uint8_t mfrc522_rfid_i2c_addr = 0x28;
const uint8_t mfrc522_rfid_i2c_irq = 4;   // Ext 1, pin 10
const uint8_t mfrc522_rfid_i2c_reset = 5; // Ext 1, pin  9

const uint8_t AARTLED_GPIO  = 15; // Ext 2, pin 8
const uint8_t PUSHBUTTON_GPIO =  1; // Ext 1, pin 6

// ACNode node = ACNode(MACHINE, WIFI_NETWORK, WIFI_PASSWD);
ACNode node = ACNode(MACHINE);

TwoWire i2cBus = TwoWire(0);

RFID reader = RFID(&i2cBus, mfrc522_rfid_i2c_addr, mfrc522_rfid_i2c_reset, mfrc522_rfid_i2c_irq);

LED aartLed = LED();    // defaults to the aartLed - otherwise specify a GPIO.

OLED oled = OLED();

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
  THANKS,
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
} state[] =
{
  { "Booting",              LED::LED_ERROR,           120 * 1000, REBOOT,         0 },
  { "Out of order",         LED::LED_ERROR,           120 * 1000, REBOOT,         5 * 60 * 1000 },
  { "Rebooting",            LED::LED_ERROR,           120 * 1000, REBOOT,         0 },
  { "Transient Error",      LED::LED_ERROR,             5 * 1000, WAITINGFORCARD, 5 * 60 * 1000 },
  { "No network",           LED::LED_FLASH,                NEVER, NOCONN,         0 },
  { "Waiting for card",     LED::LED_IDLE,                 NEVER, WAITINGFORCARD, 0 },
  { "Unknown card ?!",      LED::LED_IDLE,              5 * 1000, WAITINGFORCARD, 0 },
  { "Thanks. Bye Now !",    LED::LED_IDLE,              5 * 1000, WAITINGFORCARD, 0 },
};


unsigned long laststatechange = 0, lastReport = 0, swipeouts_count = 0;
static machinestates_t laststate = OUTOFORDER;
machinestates_t machinestate = BOOTING;

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n\n");
  Serial.println("Booted: " __FILE__ " " __DATE__ " " __TIME__ );

  pinMode(PUSHBUTTON_GPIO, INPUT_PULLUP);
  aartLed.set(LED::LED_FAST);

  i2cBus.begin(I2C_SDA_PIN, I2C_SCL_PIN); // , 50000);

  oled.setup();
  oled.setText("boot...");

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
    Log.println("Got approve - thanks");
    machinestate = THANKS;
  });
  node.onDenied([](const char * machine) {
    Log.println("Got denied - eh !?");
    machinestate = REJECTED;
  });

  node.onReport([](JsonObject  & report) {
    report["state"] = state[machinestate].label;

    report["state"] = state[machinestate].label;
    report["swipeouts"] = swipeouts_count;

#ifdef OTA_PASSWD
    report["ota"] = true;
#else
    report["ota"] = false;
#endif
  });

  reader.onSwipe([](const char * tag) -> ACBase::cmd_result_t {
    node.request_approval(tag, "leave", NULL, false);
    swipeouts_count++;
    return ACBase::CMD_CLAIMED;
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

  auto t = std::make_shared<TelnetSerialStream>(telnetSerialStream);
  Log.addPrintStream(t);
  Debug.addPrintStream(t);

  // node.set_debug(true);
  // node.set_debugAlive(true);
  node.begin(BOARD_OLIMEX);

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
    case THANKS:
      // all handled in above stage engine.
      break;
  };
}

