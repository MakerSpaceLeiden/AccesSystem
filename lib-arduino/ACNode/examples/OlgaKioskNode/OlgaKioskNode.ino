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

#ifndef PATH_SAML
#define PATH_SAML "/v3/session"
#endif
static const char url[] = TERMINAL_URL PATH_SAML;

#include <ACNode.h>
#include <OTA.h>
#include <REST/ACRestNode.h>
#include <esp_sntp.h>

// Extra RED led above reader
const uint8_t LED_INDICATOR = 5;

// i2c wired RFID reader
#include <RFID/RFID_MFRC522.h>
RFID_MFRC522 *reader = NULL;
const uint8_t MFRC_I2C_ADDDR = 0x28;
const uint8_t MFRC_NRSTPD = UNUSED_PIN;  // not connected.
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

/* grayish -- https://barth-dev.de/online/rgb565-color-picker/ */
const uint16_t GRAYISH = 0xAD55;


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


// Logging in is quite slow - so we have a special state for this.
//
MachineState::machinestate_t LOGGINGIN;
#define LOGINDELAY (10 /* seconds*/)

MachineState::machinestate_t NOCONNECTIONS, TOOMANYCONNECTIONS;

#include "WebPage.h"

String lastTag = "";

typedef enum { C_NONE,
               C_ONE,
               C_TOOMANY } ws_count_t;

ws_count_t justOneWSlisteners() {
  IPAddress ip = INADDR_NONE;
  for (std::list<AsyncWebSocketClient>::iterator it = ws.getClients().begin(); it != ws.getClients().end(); ++it) {
    if (ip == INADDR_NONE) {
      ip = it->client()->remoteIP();
      continue;
    };
    if (ip != it->client()->remoteIP()) {
      return C_TOOMANY;
    }
  };
  if (ip == INADDR_NONE) {
    return C_NONE;
  };
  return C_ONE;
};

// Check that we have just one listener. And if there
// are multiple; we reject the login. This does not
// stop network and browser shenigans - but that is fine
// as we provide the evil maid with just about anything
// she would need; including passwords already. So we are
// not making a bad situation that much worse.
//
void policeConections() {
  if (node.machinestate < MachineState::WAITINGFORCARD || node.machinestate == LOGGINGIN)
    return;

  switch (justOneWSlisteners()) {
    case C_NONE:
      if (node.machinestate != NOCONNECTIONS) {
        tft.fillScreen(ST77XX_WHITE);
        tft.setFont(&FreeSans12pt7b);
        printCentered("Open Browser on PC");
        node.machinestate = NOCONNECTIONS;
      };
      break;
    case C_ONE:
      if (node.machinestate != MachineState::WAITINGFORCARD)
        node.machinestate = MachineState::WAITINGFORCARD;
      break;
    case C_TOOMANY:
      if (node.machinestate != TOOMANYCONNECTIONS) {
        tft.fillScreen(ST77XX_WHITE);
        tft.setFont(&FreeSans12pt7b);
        printCentered("Quit other browsers");
        node.machinestate = TOOMANYCONNECTIONS;
      };
      break;
  };
};

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("setup(): " __FILE__ " " __DATE__ " " __TIME__);

  pinMode(LED_INDICATOR, OUTPUT);
  digitalWrite(LED_INDICATOR, LOW);

  setupDisplay();
  centeredText("wait", GRAYISH);

  LOGGINGIN = node.machinestate.addState((const char *)"Logging in",
                                         LED::LED_IDLE,
                                         (time_t)(LOGINDELAY * 1000),
                                         node.machinestate.WAITINGFORCARD,  // then go back to waiting for the next swipe.
                                         false /* no OTA during this */,
                                         false /* No reporting until we're done with the door. */
  );
  NOCONNECTIONS = node.machinestate.addState((const char *)"No browser connected yet",
                                             LED::LED_IDLE,
                                             MachineState::NEVER,
                                             node.machinestate.WAITINGFORCARD,
                                             true /* Ok to OTA during this */,
                                             true /* Ok to  reporting */
  );
  TOOMANYCONNECTIONS = node.machinestate.addState((const char *)"Multiple browsers connected",
                                                  LED::LED_IDLE,
                                                  MachineState::NEVER,
                                                  node.machinestate.WAITINGFORCARD,
                                                  true /* Ok to OTA during this */,
                                                  true /* Ok to  reporting */
  );

  if (!i2cBus.begin())
    Serial.println("Could not start wire(i2cBus)");

  reader = new RFID_MFRC522(&i2cBus, MFRC_I2C_ADDDR, MFRC_NRSTPD, MFRC_IRQ);
  reader->onSwipe([](const char *tag) -> ACBase::cmd_result_t {
    ACBase::cmd_result_t ret = node._restAPI->handleTagSwipe(tag);
    if (ret != ACBase::CMD_DECLINE)
      return ret;

    if (node.machinestate != MachineState::WAITINGFORCARD) {
      Log.println("Rejecting login - not waiting for a card.");
      return ACBase::CMD_CLAIMED;
    };

    node.machinestate = MachineState::CHECKINGCARD;
    lastTag = String(tag);

    digitalWrite(LED_INDICATOR, HIGH);
    ws.textAll("logging in");

    return ret;
  });
  node.addHandler(reader);

  node.onApproval([](const char *machine) {
    if (justOneWSlisteners() == C_TOOMANY) {
      Log.println("Rejecting login -- to many browsers connected");
      centeredText("SNIFF", ST77XX_RED);
      return;
    };
    if (justOneWSlisteners() == C_NONE) {
      Log.println("Rejecting login -- no browser connected");
      centeredText("NoPC", ST77XX_ORANGE);
      return;
    }
    centeredText("OK", ST77XX_DARKGREEN);
    digitalWrite(LED_INDICATOR, HIGH);

    JsonDocument saml = node._restAPI->get(url, "tag=" + lastTag);

    // Really bad idea unless we have some other security implemented or some max/count - i.e. we're giving everyone currently subscribed the Kerberos ticket.
    if (!(saml["url"].isNull()))
      ws.textAll(String(saml["url"]));
    else
      ws.textAll("Failed");

    node.machinestate = LOGGINGIN;
    lastTag = "";
  });


  node.onDenied([](const char *machine) {
    centeredText("???", ST77XX_RED);
    ws.textAll("Denied");
    lastTag = "";
  });

  node.machinestate.addOnChangeCallback(MachineState::ALL_STATES, [&](MachineState::machinestate_t last, MachineState::machinestate_t current) -> void {
    updateStatusBar(node.machinestate.label());
    // ws.textAll(node.machinestate.label());
  });

  node.machinestate.addOnChangeCallback(MachineState::WAITINGFORCARD, [](MachineState::machinestate_t oldState, MachineState::machinestate_t newState) {
    centeredText("login", GRAYISH);
  });

  OTA *ota = new OTA(ota_password_hash);
  node.addHandler(ota);

  node.onReport([](JsonObject report) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s %s %s", FILE2FIRMWARE(__FILE__), __DATE__, __TIME__);
    report["fw"] = tmp;
  });

  ws.onEvent([](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT || type == WS_EVT_DISCONNECT) {
      // Debug.println("Lost/gained a connection - checking IPs");
      // policeConections();
    };

    if (type != WS_EVT_DATA) {
      return;
    };

    AwsFrameInfo *info = (AwsFrameInfo *)arg;

    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
      if (len == 4 && strncmp((char *)data, "PING", 4) == 0) {
        client->text("ACK");
      };
    };
  });

  node.webServer()->on("/login", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", (uint8_t *)webPage, webPageLength);
  });
  node.webServer()->addHandler(&ws);

  node.begin();

  esp_sntp_servermode_dhcp(true);
  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
  tzset();

  Log.printf("Booted: %s " __DATE__ " " __TIME__, FILE2FIRMWARE(__FILE__));
}

void loop() {
  node.loop();
  loopDisplay();

  digitalWrite(LED_INDICATOR, (node.machinestate == MachineState::WAITINGFORCARD) ? 0 : ((uint8_t)(millis() / 200)) & 1);

  if (!node.isConnected())
    return;

  if (node.machinestate == MachineState::WAITINGFORCARD) {
    static unsigned long lst = 0;
    if (millis() - lst > 3000) {
      lst = millis();
      uint8_t r = 1 + (esp_random() % 99);
      char buff[3];
      snprintf(buff, sizeof(buff), "%02d", r);
      ws.textAll(buff);

      int16_t x1, y1;
      uint16_t w, h;
      tft.setFont(&FreeSansBold18pt7b);
      tft.setTextColor(ST77XX_BLACK);
      tft.getTextBounds(buff, 0, 0, &x1, &y1, &w, &h);
      const uint16_t W = 40, H = 26;
      x1 = tft.width() / 2 - W / 2;
      y1 = tft.height() / 2 - H / 2 - 8;

      tft.setCursor(x1 + (W - w) / 2, y1 + (H - h) / 2);
      tft.fillRect(x1, y1 - H + 1, W, H, GRAYISH);

      tft.print(buff);
    }
  };

  if (node.machinestate == LOGGINGIN)
    return;

  static unsigned long lst = 0;
  if (millis() - lst < 5000)
    return;

  lst = millis();

  ws.textAll(node.machinestate.label());
  policeConections();
}
