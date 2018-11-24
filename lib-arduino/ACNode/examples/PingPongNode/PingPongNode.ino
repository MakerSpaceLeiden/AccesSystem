/*
      Copyright 2015-2016 Dirk-Willem van Gulik <dirkx@webweaving.org>
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

#include <ACNode.h>

ACNode node = ACNode("pingpong"); // Force wired PoE ethernet.

MqttLogStream mqttlogStream = MqttLogStream();

#ifdef OTA_PASSWD
OTA ota = OTA(OTA_PASSWD);
#endif

typedef enum {
  BOOTING, SWERROR, OUTOFORDER, NOCONN, // some error - machine disabLED.
  RUNNING,
} machinestates_t;

const char *machinestateName[RUNNING + 1] = {
  "Software Error", "Out of order", "No network",
  "running",
};

unsigned long laststatechange = 0;
static machinestates_t laststate = OUTOFORDER;
machinestates_t machinestate = BOOTING;

unsigned long pings_send = 0;
unsigned long pings_recv = 0;

unsigned long whatsups = 0;
void setup() {
  Serial.begin(115200);
  Serial.println("\n\n\n" __FILE__ " " __DATE__ " " __TIME__);

  // the default is space.makerspaceleiden.nl, prefix test
  // node.set_mqtt_host("mymqtt-server.athome.nl");
  // node.set_mqtt_prefix("test-1234");
  node.set_master("test-master");

  // report faster than usual.
  node.set_report_period(10 * 1000);

  node.onConnect([]() {
    Log.println("Connected");
    machinestate = RUNNING;
  });
  node.onDisconnect([]() {
    Log.println("Disconnected");
    machinestate = NOCONN;
  });
  node.onError([](acnode_error_t err) {
    Log.printf("Error %d\n", err);
    machinestate = OUTOFORDER;
  });
  
  node.onValidatedCmd([](const char *cmd, const char * rest) -> ACBase::cmd_result_t  {
    if (!strcasecmp(cmd, "pong")) {
      pings_recv++;
      return ACNode::CMD_CLAIMED;
    };
    return ACBase::CMD_DECLINE;
  });

  node.onReport([](JsonObject  & report) {
    report["state"] = machinestateName[machinestate];
    report["pings_send"] = pings_send;
    report["pings_recv"] = pings_recv;

#ifdef OTA_PASSWD
    report["ota"] = true;
#else
    report["ota"] = false;
#endif
  });

#ifdef OTA_PASSWD
  node.addHandler(&ota);
#endif
  Log.addPrintStream(std::make_shared<MqttLogStream>(mqttlogStream));

  machinestate = BOOTING;
  node.begin();
}

void loop() {
  node.loop();

  switch (machinestate) {
    case BOOTING:
    case OUTOFORDER:
      break;

    case RUNNING: {
        static unsigned long last = 0;
        if (millis() - last > 5000) {
          last = millis();
          node.send("ping");
          pings_send++;
        }
      }
      break;

    case NOCONN:
    case SWERROR:
      node.delayedReboot();
      break;
  };
}
