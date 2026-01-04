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

  Board: v1.14 / Blue; with screen and a solenoid on OUT0 & 12Volt 
         instead of 220 wired to the green connector. Extra reverse
         diode over the solenoid; and big 1000uF capacitor.

  Compile settings:
  - ESP32-WROOM-DA Module (or ESP32 Dev) with minial SPIFFs
  Wiring:
  - https://wiki.makerspaceleiden.nl/mediawiki/index.php/Node_Olga_Binnen
  QR code:
  - https://wiki.makerspaceleiden.nl/mediawiki/index.php/QR_olgabinnen

Board used 
  Red dirkx/esp-rfid-old variation.
    2023/04/18 v1.01 
    Serial # 4, Rework #1

    NOTE: lower programming baudrate - chip cannot handle > 500kBaud
*/
#ifndef MACHINE
#define MACHINE "olgakiosk"
#endif

#ifndef ARDUINO_PARTITION_min_spiffs
#error "Unexpected partition table; may break OTA"
#endif
#ifndef ARDUINO_ESP32_WROOM_DA
#error "RED board based hardware is expected to be an ESP32 WROOM-DA"
#endif
#ifndef WIFI_NETWORK
#error "This node is wifi connected and needs a SSID"
#endif
#ifndef WIFI_PASSWD
#error "This node is wifi connected and needs a wifi password"
#endif

#include <ACNode.h>
#include <OTA.h>
#include <REST/ACRestNode.h>


// i2c wired RFID reader
#include <RFID/RFID_MFRC522.h>
RFID_MFRC522 *reader = NULL;
const uint8_t MFRC_I2C_ADDDR = 0x28;
const uint8_t MFRC_NRSTPD = -1;  // not connected.
const uint8_t MFRC_IRQ = 25;
const uint8_t I2C_SDA = 21;   // 21 - default
const uint8_t I2C_SCL = 22;   // 22 - default
TwoWire i2cBus = TwoWire(0);  // Bus 0

// OLED screen
const uint8_t OLED_MOSI = 12;
const uint8_t OLED_CLK = 14;
const uint8_t OLED_RST = 23;
const uint8_t OLED_DC_RS = 18;
const uint8_t OLED_CS = 0;  // Was 2
#include "SPIDisplay.h"


#ifndef OTA_PASSWD_HASH256
// Generate with 'echo -n Password | openssl sha256 or
// use https://emn178.github.io/online-tools/sha256.html.
//
// Note: No \0, cariage return or linefeed  at the end of the
//       password; just the characters of the password. So the
//       String 'Password' should yield e7cf....221a.
//
#error "An OTA password hash(sha256) MUST be set. Sorry."
#endif

const char ota_password_hash[] = OTA_PASSWD_HASH256;
ACNodeRest node(MACHINE, WIFI_NETWORK, WIFI_PASSWD);

#include "WebPage.h"

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("setup(): " __FILE__ " " __DATE__ " " __TIME__);

  if (!i2cBus.begin())
    Serial.println("Could not start wire(i2cBus)");

  reader = new RFID_MFRC522(&i2cBus, MFRC_I2C_ADDDR, MFRC_NRSTPD, MFRC_IRQ);
  reader->onSwipe([](const char *tag) -> ACBase::cmd_result_t {
    Debug.printf("Swipe detected %s\n", "***-**-****-**");

    ACBase::cmd_result_t ret = node._restAPI->handleTagSwipe(tag);
    if (ret != ACBase::CMD_DECLINE)
      return ret;

    if (node.machinestate == MachineState::WAITINGFORCARD)
      node.machinestate = MachineState::CHECKINGCARD;
    return ret;
  });
  node.addHandler(reader);

  node.onApproval([](const char *machine) {
    centeredText("OK", ST77XX_DARKGREEN);
    // bad idea unless we have some other security implemented or some max/count - i.e. we're giving everyone currently subscribed the Kerberos ticket.
    ws.textAll("OK");
  });

  node.onDenied([](const char *machine) {
    centeredText("???", ST77XX_RED);
    ws.textAll("Denied");
  });

  node.machinestate.addOnChangeCallback(MachineState::ALL_STATES, [&](MachineState::machinestate_t last, MachineState::machinestate_t current) -> void {
    tft.setFont(NULL);
    tft.setCursor(0, 0);
    tft.setTextColor(ST77XX_BLACK);
    tft.print(node.machinestate.label());
  });

  node.machinestate.addOnChangeCallback(MachineState::WAITINGFORCARD, [](MachineState::machinestate_t oldState, MachineState::machinestate_t newState) {
    centeredText("login", 0x8888 /* grayish */);
  });


  OTA *ota = new OTA(ota_password_hash);
  node.addHandler(ota);

  node.onReport([](JsonObject report) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s %s %s", FILE2FIRMWARE(__FILE__), __DATE__, __TIME__);
    report["fw"] = tmp;
  });

  ws.onEvent([](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type != WS_EVT_DATA)
      return;

    AwsFrameInfo *info = (AwsFrameInfo *)arg;

    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
      if (len == 4 && strncmp((char *)data, "PING", 4) == 0) {
        Debug.println("Returning WS Ack to ping");
        client->text("ACK");
      };
    };
  });

  node.webServer()->on("/login", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", (uint8_t *)webPage, webPageLength);
  });
  node.webServer()->addHandler(&ws);

  node.begin();

  setupDisplay();

  Log.printf("Booted: %s " __DATE__ " " __TIME__, FILE2FIRMWARE(__FILE__));
}

void loop() {
  node.loop();
  loopDisplay();

  static unsigned long lst = 0;
  if (millis() - lst < 2000)
    return;
  lst = millis();
  ws.textAll(node.machinestate.label());
}
