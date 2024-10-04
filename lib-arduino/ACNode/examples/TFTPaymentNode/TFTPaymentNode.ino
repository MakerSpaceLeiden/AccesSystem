// dirkx@webweaving.org, apache license, for the makerspaceleiden.nl
//
// https://sites.google.com/site/jmaathuis/arduino/lilygo-ttgo-t-display-esp32
// https://www.otronic.nl/a-59613972/esp32/esp32-wroom-4mb-devkit-v1-board-met-wifi-bluetooth-en-dual-core-processor/
// with a tft 1.77 arduino 160x128 RGB display
// https://www.arthurwiz.com/software-development/177-inch-tft-lcd-display-with-st7735s-on-arduino-mega-2560
//
// Tools settings:
//  Board ESP32 Dev module
//  Upload Speed: 921600 (or half that on MacOSX!)
//  CPU Frequency: 240Mhz (WiFi/BT)
//  Flash Frequency: 80Mhz
//  Flash Mode: QIO
//  Flash Size: 4MB (32Mb)
//  Partition Scheme: Default 4MB with spiffs (1.2MB APP/1.5 SPIFFS)
//  Core Debug Level: None
//  PSRAM: Disabled
//  Port: [the COM port your board has connected to]
//
// Additional Librariries (via Sketch -> library manager):
///   TFT_eSPI

char terminalName[64];

#include <WiFi.h>
#include <WiFiUdp.h>
#include <HTTPClient.h> // for the HTTP error codes.
#include <ESPmDNS.h>
#include "esp_heap_caps.h"

#include <ACRestNode.h>

IODebounce * btn1, * btn2;

int update = false;

int NA = 0;
char **amounts = NULL;
char **prices = NULL;
char **descs = NULL;
int amount = 0;
int default_item = -1;
double amount_no_ok_needed = AMOUNT_NO_OK_NEEDED;
const char * version = VERSION;
double paid = 0;

state_t md = BOOT;
board_t BOARD = BOARD_V4;

// Updates the small clock in the top right corner; and
// will reboot the unit early mornings.
//
static void loop_RebootAtMidnight() {
  static unsigned long lst = millis();
  if (millis() - lst < 60 * 1000)
    return;
  lst = millis();

  static unsigned long debug = 0;
  if (millis() - debug > 60 * 60 * 1000) {
    debug = millis();
    time_t now = time(nullptr);
    char * p = ctime(&now);
    p[5 + 11 + 3] = 0;
  }
  time_t now = time(nullptr);
  if (now < 3600)
    return;

#ifdef AUTO_REBOOT_TIME
  static unsigned long reboot_offset = random(3600);
  char * q = ctime(&now);
  now += reboot_offset;
  char * p = ctime(&now);
  p += 11;
  p[5] = 0;

  if (strncmp(p, AUTO_REBOOT_TIME, strlen(AUTO_REBOOT_TIME)) == 0 && millis() > 3600UL) {
    Log.printf("Nightly reboot of %s has come - also to fetch new pricelist and fix any memory leaks.\n", q);
    yield();
    delay(1000);
    yield();
    ESP.restart();
  }
#endif
}

void settupButtons()
{
  if (BOARD != BOARD_V2) {
    // buttons with pullup; wired to GND
    btn1 = new IODebounce(BUTTON_1, INPUT_PULLUP);
    btn2 = new IODebounce(BUTTON_2, INPUT_PULLUP);
  } else {
    // buttons wired to VCC.
    btn1 = new IODebounce(BUTTON_1, INPUT, false, false /* active high, normal low */);
    btn2 = new IODebounce(BUTTON_2, INPUT, false, false /* active high, normal low */);
  };
  btn1->setPressedHandler([](Button2 & b) {
    if (md == SCREENSAVER) {
      update = true;
      return;
    };
    int l = amount;
    if (md == ENTER_AMOUNT && NA)
#ifndef ENDLESS
      if (amount + 1 < NA)
#endif
        amount = (amount + 1) % NA;
    if (md == OK_OR_CANCEL)
      md = DID_OK;

    update = update || (md != ENTER_AMOUNT) || (l != amount);
    if (l != amount)
      Debug.println("left");
  });

  btn2->setPressedHandler([](Button2 & b) {
    if (md == SCREENSAVER) {
      update = true;
      return;
    };
    int l = amount;
    if (md == ENTER_AMOUNT && NA)
#ifndef ENDLESS
      if (amount > 0 )
#endif
        amount = (amount + NA - 1) % NA;
    if (md == OK_OR_CANCEL)
      md = DID_CANCEL;

    update = update || (md != ENTER_AMOUNT) || (l != amount);

    if (l != amount)
      Debug.println("right");
  });

  node.addHandler(btn1);
  node.addHandler(btn2);
}

static unsigned char normal_led_brightness = NORMAL_LED_BRIGHTNESS;
#ifdef LED_1
void setupLEDS()
{
  pinMode(LED_1, OUTPUT);
  pinMode(LED_2, OUTPUT);
  digitalWrite(LED_1, (BOARD == BOARD_V2) ? HIGH : LOW);
  digitalWrite(LED_2, (BOARD == BOARD_V2) ? HIGH : LOW);
  delay(50);
  ledcSetup(1, 2500, 8);
  ledcAttachPin(LED_1, 1);
  ledcSetup(2, 2500, 8);
  ledcAttachPin(LED_1, 2);
  led_loop(md);
}
#else
void setupLEDS() {}
#endif

void led_loop(state_t md) {
#ifdef LED_1
  switch (md) {
    case ENTER_AMOUNT:
#ifdef ENDLESS
      // Visibly dim the buttons at the end of the strip; as current 'wrap around'
      // is not endless.
      ledcWrite(1, normal_led_brightness);
      ledcWrite(2, normal_led_brightness);
#else
      ledcWrite(1, amount + 1 == NA ? normal_led_brightness : (BOARD == BOARD_V2) ? HIGH : LOW);
      ledcWrite(2, amount == 0 ? normal_led_brightness : (BOARD == BOARD_V2) ? HIGH : LOW);
#endif
      break;
    case OK_OR_CANCEL:
      digitalWrite(LED_1, (BOARD == BOARD_V2) ? HIGH : LOW);
      digitalWrite(LED_2, (BOARD == BOARD_V2) ? HIGH : LOW);
      break;
    default:
      // Switch them off - nothing you can do here.
      digitalWrite(LED_1, (BOARD == BOARD_V2) ? LOW : HIGH);
      digitalWrite(LED_2, (BOARD == BOARD_V2) ? LOW : HIGH);
      break;
  };
#endif
}

static board_t detectBoard() {
  unsigned char mac[6];
  WiFi.macAddress(mac);

  // C8:C9:A3:CB:B6:7C - Grijpvoorraad - buttons aan '+'
  if (mac[3] == 0xCB && mac[4] == 0xB6 && mac[5] == 0x7C) {
    normal_led_brightness = 255;
    return BOARD_V2;
  };

  // 80:7D:3A:D5:46:8C - Voorruimte - buttons aan GND
  if (mac[3] == 0xD5 && mac[4] == 0x46 && mac[5] == 0x8C)
    return BOARD_V3;

  // test board - scherm 180 graden gedraaid.
  normal_led_brightness = 255;
  return BOARD_V4;
};

static const char * board2name(board_t x) {
  switch (x) {
    case BOARD_V2: return "v3: buttons VCC, LEDs on LOW";
    case BOARD_V3: return "v4: buttons GND, LEDs on HIGH";
    case BOARD_V4: return "v5: buttons GND, LEDs on HIGH, Screen flipped";
    default: break;
  }
  return "Dunno";
}

bool isPaired = false;
void setup()
{
  const char * p = __FILE__;
  if (rindex(p, '/')) p = rindex(p, '/') + 1;

  Serial.begin(115200);
  byte mac[6];
  WiFi.macAddress(mac);
  snprintf(terminalName,  sizeof(terminalName), "%s-%s-%02x%02x%02x", TERMINAL_NAME, VERSION, mac[3], mac[4], mac[5]);

  BOARD = detectBoard();
  Serial.printf( "File:     %s\n", p);
  Serial.println("Version : " VERSION);
  Serial.println("Compiled: " __DATE__ " " __TIME__);
  Serial.printf( "Revision: %s\n", board2name(BOARD));
  Serial.printf( "Heap    :  %d Kb\n",   (512 + heap_caps_get_free_size(MALLOC_CAP_INTERNAL)) / 1024UL);
  Serial.print(  "MacAddr:  ");
  Serial.println(WiFi.macAddress());

#ifdef LED_1
  setupLEDS();
#endif
  setupTFT();
  md = BOOT;
  updateDisplay(BOOT);

  settupButtons();
  isPaired = setupAuth(terminalName);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_NETWORK, WIFI_PASSWD);
  WiFi.setHostname(terminalName);

  setupWiFiConnectionOrReboot();
  setupLog();
  setupOTA();

  // Do this late - so that this information also is visible
  // in (remote) logging.
  //
  if (!setupRFID()) {
    displayForceShowErrorModal("RFID", "Scanner not found");
    log_loop();
    yield();
    delay(5000);
    ESP.restart();
  };

  // try to get some reliable time; to stop my cert
  // checking code complaining.
  //
  configTzTime(cestTimezone, NTP_POOL);

  Log.printf( "File:     %s\n", p);
  Log.println("Firmware: " TERMINAL_NAME "-" VERSION);
  Log.println("Build:    " __DATE__ " " __TIME__ );
  Log.print(  "Unit:     ");
  Log.println(terminalName);
  Log.printf( "Revision: %s\n", board2name(BOARD));
  Log.print(  "MacAddr:  ");
  Log.println(WiFi.macAddress());
  Log.print(  "IP:       ");
  Log.println(WiFi.localIP());
  Log.printf("Paired:    ", isPaired ? "yes" : "no");
  Log.printf("Heap:     %d Kb\n",   (512 + heap_caps_get_free_size(MALLOC_CAP_INTERNAL)) / 1024UL);
  Serial.println();
  Log.println("Starting loop");
  md = WAITING_FOR_NTP;
}

void loop()
{
  static unsigned long lastchange = -1;
  static state_t laststate = WAITING_FOR_NTP;
  static int last_amount = -1;
  unsigned int extra_show_delay = 0;

  log_loop();
  loop_RebootAtMidnight();
  ota_loop();
  button_loop();
#ifdef LED_1
  led_loop(md);
#endif


  switch (md) {
    case Machinestate::BOOTING:
        // not going well - warn the user and try for a bit,
        // while communicating a timeout with a progress bar.
        //
        updateDisplay_startProgressBar("Waiting for Wifi");
        static unsigned long lst = millis();
        float p = ((float)(millis()-lst) / WIFI_MAX_WAIT);
        if (WiFi.isConnected()) {
            p = 1.0;
            node.machinestate =WAITING_FOR_NTP;
            };
        updateDisplay_progressBar(p);
        if (p > 1.0)
            node.machinestate = Machinestate::REBOOT:;
        break:
    case Machinestate::REBOOT:
        displayForceShowErrorModal((char *)"reboot", "WiFi problem");
        delay(2500);
        ESP.restart();
        break;
    case WAITING_FOR_NTP:
      if (lastchange == -1)
        updateDisplay_progressText("waiting for time");
      if (time(nullptr) > 3600)
        md = FETCH_CA;
      break;
    case FETCH_CA:
      if (fetchCA())
        md = isPaired ? REGISTER_PRICELIST : REGISTER;
      break;
    case REGISTER:
      if (registerDevice())
        md = WAIT_FOR_REGISTER_SWIPE;
      break;
    case WAIT_FOR_REGISTER_SWIPE:
      if (loopRFID())
        if (registerDeviceWithSwipe(tag)) {
          isPaired = true;
          md = REGISTER_PRICELIST;
        };
      break;
    case REGISTER_PRICELIST:
      { int httpCode = fetchPricelist();
        if (httpCode = HTTP_CODE_OK)
          md = ENTER_AMOUNT;
        else if (httpCode = HTTP_CODE_BAD_REQUEST)
          md = REGISTER; // something gone very wrong server side - simply reset/retry.
        break;
      };
    case SCREENSAVER:
      if (update) {
        Log.println("Wakeup");
        setTFTPower(true);
        md = ENTER_AMOUNT;
        lastchange = millis(); // prevent jump to default straight after wakeup.
      };
      break;
    case ENTER_AMOUNT:
      if (millis() - lastchange > SCREENSAVER_TIMEOUT) {
        md = SCREENSAVER;
        Log.println("Enabling screensaver (from enter)");
        setTFTPower(false);
        return;
      };
      if (default_item >= 0 && millis() - lastchange > DEFAULT_TIMEOUT && amount != default_item && amount == last_amount) {
        last_amount = amount = default_item;
        lastchange = millis();
        Log.printf("Jumping back to default item %d: %s\n", default_item, amounts[default_item]);
        update = true;
      };
      memset(tag, 0, sizeof(tag));
      if (NA > 0) {
        if (loopRFID()) {
          md = (atof(prices[amount]) < AMOUNT_NO_OK_NEEDED) ?  DID_OK : OK_OR_CANCEL;
          update = true;
        };
      };
      break;
    case OK_OR_CANCEL:
      // time out handled by generic timeout.
      break;
    case DID_CANCEL:
      displayForceShowErrorModal("abort", "payment cancelled");
      md = ENTER_AMOUNT;
      memset(tag, 0, sizeof(tag));
      break;
    case DID_OK:
      {
        char buff[48], who[PBR_LEN];
        displayForceShow("paying", "...");

        int rc = payByREST(tag, prices[amount], descs[amount], who);
        if (rc != HTTP_CODE_OK) {
          snprintf(buff, sizeof(buff), "no payment - %03d", rc);
          displayForceShowErrorModal("FAIL", buff);
          md = ENTER_AMOUNT;
        } else {
          displayForceShowModal("PAID", who);
          extra_show_delay = 1500;
          md = ENTER_AMOUNT;
          Log.printf("Paid %.2f\n", atof(prices[amount]));
          paid += atof(prices[amount]);
        };
        memset(tag, 0, sizeof(tag));
      };
      break;
  };

  if (md != laststate || last_amount != amount) {
    laststate = md;
    lastchange = millis();
    last_amount = amount;
    update = true;
  }

  // generic time/error out if something takes longer than 1- seconds and we are in a state
  // where one does not expect this.
  //
  if ((millis() - lastchange > 10 * 1000 && md > ENTER_AMOUNT )) {
    if (md == OK_OR_CANCEL) {
      displayForceShowModal("canceling", NULL);
      md = ENTER_AMOUNT;
    }
    else {
      displayForceShowErrorModal("Timeout", NULL);
    };
    update = true;
  };

  if (update) {
    update = false;
    updateDisplay(md);
  } else {
    updateClock(false);
  };
  delay(extra_show_delay);
}
