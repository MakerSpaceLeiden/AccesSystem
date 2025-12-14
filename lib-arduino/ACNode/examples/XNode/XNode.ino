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

   Compile settings:  ESP32-WROOM-DA Module (or ESP32 Dev)

   Chip type:          ESP32-D0WD-V3 (revision v3.1)

  QR code shown:

   https://wiki.makerspaceleiden.nl/mediawiki/index.php/QR_lintzaag

   2025/02/10 - changes freom a 1.08 white not to a newer blue board
*/
#include <BlueNodev114.h>

#ifndef ARDUINO_PARTITION_min_spiffs
#error "Unexpected partition table; may break OTA"
#endif
#ifndef ARDUINO_ESP32_WROOM_DA
#error "Hardware is expected to be an ESP32 WROOM-DA"
#endif

#ifndef MACHINE
#define MACHINE "xnode"  
#endif

#ifndef OTA_PASSWD_HASH256
#error "An OTA password hash(md5) MUST be set. Sorry."
#endif
const char ota_password_hash[] = OTA_PASSWD_HASH256;

BlueNodev114 node = BlueNodev114(MACHINE, WIFI_NETWORK, WIFI_PASSWD);

static void tellOff(const char *msg) {
  node.updateDisplay(msg, "", "");
  Log.printf("Telling=off: %s\n", msg);
  for (int i = 0; i < 9; i++) {
    node.buzzerErr();
    delay(300);
  };
}


void setup() {
  Serial.begin(115200);
  Serial.println("\n\n\n");
  Log.printf("\nBooting(): %s " __DATE__ " " __TIME__ "\n", FILE2FIRMWARE(__FILE__));

  node.setOTAPasswordHash(ota_password_hash);

  node.onReport([](JsonObject &report) {
    char buff[128];
    snprintf(buff, sizeof(buff), "%s " __DATE__ " " __TIME__, FILE2FIRMWARE(__FILE__));
    report["fw"] = buff;
  });

  node.begin();
  Log.printf("Starting loop(): %s " __DATE__ " " __TIME__ "\n", FILE2FIRMWARE(__FILE__));
}

void loop() {
  node.loop();

  static unsigned long lst = 0;
  if (millis() > lst + 1000 && node.machinestate == MachineState::WAITINGFORCARD) {
    lst = millis();
    static unsigned char i = 0;
    char buff[124];
    snprintf(buff,sizeof(buff),"%02X", ++i);
    node.updateDisplayStateMsg(buff, 2);
  }
};
