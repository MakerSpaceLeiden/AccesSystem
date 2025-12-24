/*
      Copyright 2015-2025 Dirk-Willem van Gulik <dirkx@webweaving.org>
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

  Board: v1.14 / Blue; 
    AW chip removed - something amiss with soldering or traces.

  Board:   # 04
  RFID:    Firmware Version: 0x92 = v2.0
  Chip:    00303468 ESP32-D0WD-V3 r301 #2
  Wifi     94:51:DC:30:34:68
  Ethernet 94:51:DC:30:34:6B

  Compile settings:
  - ESP32-WROOM-DA Module (or ESP32 Dev) with minial SPIFFs
  Wiring:
  - https://wiki.makerspaceleiden.nl/mediawiki/index.php/Node_ByeBye_Olga
  QR code:
  - https://wiki.makerspaceleiden.nl/mediawiki/index.php/QR_byebyeolga

*/

#include <BlueNodev114.h>

#define MACHINE "byebyeolga"

// Generate with 'echo -n Password | openssl sha256 or
// use https://emn178.github.io/online-tools/sha256.html.

// Note: No \0, cariage return or linefeed  at the end of the
//       password; just the characters of the password. The
//       String 'Password' should yield e7cf....221a.
//
#ifndef OTA_PASSWD_HASH256
#error "An OTA password hash(sha256) MUST be set. Sorry."
#endif

const char ota_password_hash[] = OTA_PASSWD_HASH256;
BlueNodev114 node = BlueNodev114(MACHINE);

unsigned long byebye_count = 0, byebye_denied_count = 0;

MachineState::machinestate_t BYEBYE, REJECTED;

void setup() {
  Serial.println("setup(): " __FILE__ " " __DATE__ " " __TIME__);

  BYEBYE = node.machinestate.addState((const char *)"Thanks & bye", LED::LED_IDLE, (time_t)(5 * 1000), node.machinestate.WAITINGFORCARD);
  REJECTED = node.machinestate.addState((const char *)"Euh?!", LED::LED_ERROR, (time_t)(5 * 1000), node.machinestate.WAITINGFORCARD);

  node.setOnChangeCallback(BYEBYE, [&](MachineState::machinestate_t last, MachineState::machinestate_t current) -> void {
    ApprovalEntry *e = node.lastApproved();
    if (!e)
      return;

    const char *name = NULL;

    if (e && e->shortName)
      name = e->shortName.c_str();
    else if (e && e->name)
      name = e->name.c_str();

    node.updateDisplayStateMsg(name, 2);
    Log.printf("Saying bye to %s\n", name);
  });

  node.onApproval([](const char *machine) {
    node.buzzerOk();
    node.machinestate = BYEBYE;
    byebye_count++;
  });

  node.onDenied([](const char *machine) {
    node.machinestate = REJECTED;
    node.buzzerErr();
    byebye_denied_count++;
  });

  node.setOTAPasswordHash(ota_password_hash);

  node.onReport([](JsonObject report) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s %s %s", FILE2FIRMWARE(__FILE__), __DATE__, __TIME__);
    report["fw"] = tmp;
    report["byebye_count"] = byebye_count;
    report["byebye_denied_count"] = byebye_denied_count;
  });

  node.begin();

  Log.printf("Booted: %s " __DATE__ " " __TIME__, FILE2FIRMWARE(__FILE__));
}

void loop() {
  node.loop();
}
