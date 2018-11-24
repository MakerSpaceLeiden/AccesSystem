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
#include <SyslogStream.h>
#include <MqttLogStream.h>

ACNode node = ACNode(true); // Force wired PoE ethernet.

SyslogStream syslogStream = SyslogStream();
MqttLogStream mqttlogStream = MqttLogStream("test", "moi");

OTA ota = OTA("FooBar");

MSL trivialSecurityHandler = MSL();

typedef enum {
  BOOTING, SWERROR, OUTOFORDER, NOCONN, // some error - machine disabLED.
  RUNNING,
  NOTINUSE
} machinestates_t;

const char *machinestateName[NOTINUSE] = {
  "Software Error", "Out of order", "No network",
  "running",
  "== not in use == "
};

unsigned long laststatechange = 0;
static machinestates_t laststate = OUTOFORDER;
machinestates_t machinestate = BOOTING;

unsigned long whatsups = 0;
void setup() {
  Serial.begin(115200);
  Serial.println("\n\n\n" __FILE__ " " __DATE__ " " __TIME__);

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
  node.onValidatedCmd([](const char *cmd, const char *restl) {
    if (!strcasecmp("whatsup", cmd)) {
      send(NULL, "nuffing");
      whatsups++;
    } else {
      Log.printf("Unhandled command <%s> -- ignored.", cmd);
    }
  });

  node.addHandler(ota);
  node.addSecurityHandler(trivialSecurityHandler);

  // default syslog port and destination (gateway address or broadcast address).
  //
  Debug.addPrintStream(std::make_shared<SyslogStream>(syslogStream));
  Log.addPrintStream(std::make_shared<SyslogStream>(syslogStream));

  // assumes the client connection for MQTT (and network, etc) is up - otherwise silenty fails/buffers.
  //
  Log.addPrintStream(std::make_shared<MqttLogStream>(mqttlogStream));

  machinestate = BOOTING;

  node.set_debug(true);
  node.set_debugAlive(true);

  node.begin();
}

void loop() {
  node.loop();

  if (laststate != machinestate) {
    laststate = machinestate;
    laststatechange = millis();
  }

  switch (machinestate) {
    case NOCONN:
      node.delayedReboot();
      break;
    case BOOTING:
      if (node.isUp())
        machinestate = RUNNING;
      break;
    case OUTOFORDER:
    case SWERROR:
      node.delayedReboot();
      break;
    case RUNNING:
      break;
    case NOTINUSE:
      break;
  };

  {
    static unsigned long lastreport = 0;
    if (millis() - lastreport > 5000) {
      Debug.printf("Report (every 5 seconds) -- state: %d:%s, whatsups: %lu\n",
                   machinestate, machinestateName[machinestate], whatsups);
    }
  }
}

