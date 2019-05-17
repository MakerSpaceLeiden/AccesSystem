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

#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Wire.h>

#include <ACNode.h>
#include <RFID.h>
#include "OLED.h"
#include "MachineState.h"

// #include "acmerootcert.h"

#define MACHINE             "byebye"

const uint8_t I2C_SDA_PIN = 13; // i2c SDA Pin, ext 2, pin 10
const uint8_t I2C_SCL_PIN = 16; // i2c SCL Pin, ext 2, pin 7

const uint8_t mfrc522_rfid_i2c_addr = 0x28; // configured on the reader board itself.
const uint8_t mfrc522_rfid_i2c_irq = 4;   // Ext 1, pin 10
const uint8_t mfrc522_rfid_i2c_reset = 5; // Ext 1, pin  9

const uint8_t AARTLED_GPIO  = 15; // Ext 2, pin 8
const uint8_t PUSHBUTTON_GPIO =  1; // Ext 1, pin 6

// ACNode node = ACNode(MACHINE, WIFI_NETWORK, WIFI_PASSWD);
ACNode node = ACNode(MACHINE);

TwoWire i2cBus = TwoWire((uint8_t)0);

RFID reader = RFID(&i2cBus, mfrc522_rfid_i2c_addr, mfrc522_rfid_i2c_reset, mfrc522_rfid_i2c_irq);
LED aartLed = LED();    // defaults to the aartLed - otherwise specify a GPIO.
OLED oled = OLED();
OTA ota = OTA(OTA_PASSWD);

MqttLogStream mqttlogStream = MqttLogStream();
TelnetSerialStream telnetSerialStream = TelnetSerialStream();

MachineState machinestate = MachineState();
MachineState::machinestates_t BYEBYE, REJECTED;

unsigned long swipeouts_count = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n\n");
  Serial.println("Booted: " __FILE__ " " __DATE__ " " __TIME__ );

  pinMode(PUSHBUTTON_GPIO, INPUT_PULLUP);
  aartLed.set(LED::LED_FAST);

  // i2C Setup for OLED and RFID
  i2cBus.begin(I2C_SDA_PIN, I2C_SCL_PIN); // , 50000);

  node.set_mqtt_prefix("ac");
  node.set_master("master");

  BYEBYE = machinestate.addState("Thanks & bye now !", LED::LED_IDLE, 5 * 1000, machinestate.WAITINGFORCARD);
  REJECTED = machinestate.addState("Euh?!", LED::LED_ERROR, 5 * 1000, machinestate.WAITINGFORCARD);

  // Update the display whenever we enter into a new state.
  //
  machinestate.setOnChangeCallback(MachineState::ALL_STATES, [](MachineState::machinestates_t last, MachineState::machinestates_t current) -> void {
    oled = machinestate.label();
  });

  node.onConnect([]() {
    Log.println("Connected");
    machinestate = MachineState::WAITINGFORCARD;
  });
  node.onDisconnect([]() {
    Log.println("Disconnected");
    machinestate = MachineState::NOCONN;

  });
  node.onError([](acnode_error_t err) {
    Log.printf("Error %d\n", err);
    machinestate = MachineState::WAITINGFORCARD;
  });

  node.onApproval([](const char * machine) {
    machinestate = BYEBYE;
  });

  node.onDenied([](const char * machine) {
    machinestate = REJECTED;
  });

  node.onReport([](JsonObject  & report) {
    report["swipeouts"] = swipeouts_count;
  });

  reader.onSwipe([](const char * tag) -> ACBase::cmd_result_t {
    node.request_approval(tag, "leave", NULL, false);
    machinestate = MachineState::CHECKINGCARD;
    swipeouts_count++;
    return ACBase::CMD_CLAIMED;
  });

  // This reports things such as FW version of the card; which can 'wedge' it. So we
  // disable it unless we absolutely positively need that information.
  //
  reader.set_debug(false);
  node.addHandler(&reader);
  node.addHandler(&oled);
  node.addHandler(&ota);
  node.addHandler(&machinestate);

  Log.addPrintStream(std::make_shared<MqttLogStream>(mqttlogStream));

  auto t = std::make_shared<TelnetSerialStream>(telnetSerialStream);
  Log.addPrintStream(t);
  Debug.addPrintStream(t);

  // node.set_debug(true);
  // node.set_debugAlive(true);
  node.begin(BOARD_OLIMEX); // OLIMEX

  oled = "booting...";

  Log.println("Booted: " __FILE__ " " __DATE__ " " __TIME__ );
}

#if 0
void fetchAndUpdateState() {
  WiFiClientSecure *client = new WiFiClientSecure;
  if (!client) {
    Log.println("Failed to client for fetch of state from server.");
    return;
  };

  client->setCACert(rootCACertificate);

  HTTPClient https;
  if (!https.begin(*client, CRM_SPACE_URL "/api/v1/info" )) {
    Log.println("Failed to create fetch of state from server.");
    return;
  }

#ifdef CRM_SPACE_BEARER_TOKEN
  https.addHeader("Authorization", "Bearer " CRM_SPACE_BEARER_TOKEN);
#endif

  int httpCode = https.GET();

  if (httpCode != 200) {
    Log.printf("[HTTPS] GET... failed, error: %s\n", https.errorToString(httpCode).c_str());
    return;
  };

  String payload = https.getString();

  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& root = jsonBuffer.parseObject(json);
  if (!root.success()) {
    Serial.println("Parse of state-json failed");
    return;
  }

  /* {
     "machines" : [
        "Main room lights", "Woodlathe"
     ],
     "members" : [
        "Peter", "Petra,
     ],
     "lights" : []
    }
  */
  oled.setText(payload.c_str());

  https.end();
  return;
}
#endif

void loop() {
  node.loop();
}
