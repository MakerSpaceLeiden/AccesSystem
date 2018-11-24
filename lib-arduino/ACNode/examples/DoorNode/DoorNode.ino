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

#define SOLENOID  (4)

// Labelling as per `blue' RFID MFRC522-MSL 1471 'fixed'
#define MFRC522_SDA     (15)
#define MFRC522_SCK     (14)
#define MFRC522_MOSI    (13)
#define MFRC522_MISO    (12)
#define MFRC522_IRQ     (33)
#define MFRC522_GND     /* gnd pin */
#define MFRC522_RSTO    (32)
#define MFRC522_3V3     /* 3v3 */

// Wired ethernet uses hte
ACNode node = ACNode("a-door");
RFID reader = RFID(MFRC522_SDA, MFRC522_RSTO, MFRC522_IRQ, MFRC522_SCK, MFRC522_MISO, MFRC522_MOSI);

MqttLogStream mqttlogStream = MqttLogStream();

#ifdef OTA_PASSWD
OTA ota = OTA(OTA_PASSWD);
#endif

typedef enum {
  BOOTING, SWERROR, OUTOFORDER, NOCONN, // some error - machine disabLED.
  WAITINGFORCARD,             // waiting for card (or checking one)
  BUZZING,                    // this is where we engage the solenoid.
} machinestates_t;

const char *machinestateName[BUZZING + 1] = {
  "Booting", "Software Error", "Out of order", "No network",
  "Waiting for card",
  "Buzzing door"
};

unsigned long laststatechange = 0;
static machinestates_t laststate = OUTOFORDER;
machinestates_t machinestate = BOOTING;

unsigned long opening_door_count  = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n\n");
  Serial.println("Booted: " __FILE__ " " __DATE__ " " __TIME__ );

  // Init the hardware and get it into a safe state.
  //
  pinMode(SOLENOID, OUTPUT);
  digitalWrite(SOLENOID, 0);

  // the default is space.makerspaceleiden.nl, prefix test
  // node.set_mqtt_host("mymqtt-server.athome.nl");
  // node.set_mqtt_prefix("test-1234");
  node.set_master("test-master");
  node.set_report_period(10 * 1000);

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
  });
  node.onReport([](JsonObject  & report) {
    report["state"] = machinestateName[machinestate];
    report["opening_door_count"] = opening_door_count;

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

  // node.set_debug(true);
  // node.set_debugAlive(true);
  node.begin();
  Log.println("Booted: " __FILE__ " " __DATE__ " " __TIME__ );
}

void loop() {
  node.loop();

  if (laststate != machinestate) {
    laststate = machinestate;
    laststatechange = millis();
    if (machinestate == BUZZING)
      opening_door_count++;
  }

  switch (machinestate) {
    case WAITINGFORCARD:
      digitalWrite(SOLENOID, 0);
      break;

    case BUZZING:
      digitalWrite(SOLENOID, 1);
      if ((millis() - laststatechange) > 5000)
        machinestate = WAITINGFORCARD;
      break;

    case BOOTING:
    case OUTOFORDER:
    case SWERROR:
    case NOCONN:
      if ((millis() - laststatechange) > 120 * 1000)
        node.delayedReboot();
      break;
  };
}
