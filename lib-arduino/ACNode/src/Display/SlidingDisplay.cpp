#include "global.h"
#include "display.h"
#include "rfid.h"
#include "rest.h"
#include "log.h"

#include "NotoSansMedium8.h"
#define AA_FONT_TINY  NotoSansMedium8

#include "NotoSansMedium12.h"
#define AA_FONT_SMALL NotoSansMedium12

#include "NotoSansBold15.h"
#define AA_FONT_MEDIUM NotoSansBold15

#include "NotoSansMedium20.h"
#define AA_FONT_LARGE NotoSanMedium20

// #include "NotoSansBold36.h"
// #define AA_FONT_HUGE  NotoSansBold36

#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0
#include "bmp.c"

TFT_eSPI tft = TFT_eSPI(TFT_WIDTH, TFT_HEIGHT);
TFT_eSprite spr = TFT_eSprite(&tft);

void setupTFT() {
#ifdef TFT_BL
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, (!TFT_BACKLIGHT_ON));
#endif
  tft.init();
  tft.setRotation((BOARD == BOARD_V4) ? TFT_ROTATION - 2 : TFT_ROTATION);
#ifndef _H_BLUEA160x128
  tft.setSwapBytes(true);
#endif
#ifdef SPRITESCROLL
  spr.createSprite(2 * tft.width(), 68);
#else
  spr.createSprite(1 * tft.width(), 68);
#endif
  setTFTPower(true);
}

void wifiIcon(int32_t x, int32_t  y) {
  float ss = WiFi.RSSI();
  if (!WiFi.isConnected() || ss == 0) {
    tft.drawTriangle(x, y + 6, x + 10, y, x + 10, y + 6, TFT_RED);
    return;
  };

  // Range is from -80 to -10 or so. Above 60 is ok.
  ss = 5 * (75. + ss) / 30;

  tft.fillRect(x, y, 10, 6, TFT_BLACK);
  for (int s = 0; s < 5; s++) {
    int32_t h = (s + 1 <  ss) ? (s + 2) : 1;
    tft.fillRect(x + s * 2, y + 6 - h, 1, h,  (s + 1 <  ss) ? TFT_WHITE : TFT_YELLOW);
  };
};

static void showLogo() {
  tft.pushImage(
    (tft.width() - msl_logo_map_width) / 2, 0, // (tft.height() - msl_logo_map_width) ,
    msl_logo_map_width, msl_logo_map_width,
    (uint16_t *)  msl_logo_map);
}

// We keep the price planel on a sprite; so it mecomes easier
// scroll smoothly without flickering as we do not have enough
// memory for double buffering.
//
static void drawPricePanel(int offset, int amount) {
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.loadFont(AA_FONT_LARGE);
  spr.drawString(amounts[amount], offset + tft.width() / 2, 0);
  spr.setTextColor(TFT_YELLOW, TFT_BLACK);

  spr.loadFont(AA_FONT_SMALL);
  spr.drawString(descs[amount], offset + tft.width() / 2, 39);

  spr.loadFont(AA_FONT_LARGE);
  spr.drawString(prices[amount], offset + tft.width() / 2, spr.height() - 16); // we know it is only digits.
};

static void drawPricePanels(int left, int right) {
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  drawPricePanel(0, left);
  drawPricePanel( tft.width() , right);
}

static int slide_speed(int x) {
#if SPRING_STYLE
  int s = fabs(x -  tft.width() / 2) / 10; // <-- spring style
#else
  int s = abs(tft.width() / 2 - fabs(x -  tft.width() / 2)) / 10; // <-- click style
#endif
  if (s < 1) s = 1;
  return s;
}

static void scrollpanel_loop() {
#ifdef SPRITESCROLL
  static int last_amount = amount;
  if (amount == last_amount) {
#endif
    drawPricePanels(amount, amount);
    spr.pushSprite(0, 32);
    return;
#ifdef SPRITESCROLL
  };
  if ((amount - last_amount + NA) % NA == 1) {
    drawPricePanels(last_amount, amount);
    for (int x = 0; x < tft.width() + 4 /* intentional overshoot */;  x += slide_speed(x)) {
      spr.pushSprite(-x, 32 );
    }
    spr.pushSprite(tft.width(), 32); // Snap back
  } else {
    drawPricePanels( amount, last_amount);
    for (int x = 0; x < tft.width() + 4 /* intentional overshoot */; x += slide_speed(x)) {
      spr.pushSprite(-tft.width() + x, 32 );  // Snap back
    }
    spr.pushSprite(0, 32);
  }
  last_amount = amount;
#endif
}

static unsigned short lastw = -1;
void  updateDisplay_startProgressBar(const char *str) {
  tft.fillScreen(TFT_BLACK);
  showLogo();
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.loadFont(AA_FONT_LARGE);
  tft.drawString(str, tft.width() / 2, tft.height() / 2 - 10);
  lastw = -10;
  tft.drawRect(20, tft.height() - 40, tft.width() - 40, 20, TFT_WHITE);
  updateDisplay_progressBar(0.0);
};

void updateDisplay_progressBar(float p)
{ unsigned short l = tft.width() - 48 - 4;
  unsigned short w = l * p;
  if (w == lastw) return;
  tft.fillRect(20 + 2, tft.height() - 40 + 2, w, 20 - 4, TFT_GREEN);
  lastw = w;
};

void updateDisplay(state_t md)
{
  // Serial.printf("Update %d\n", md);
  tft.fillScreen(TFT_BLACK);
  updateClock(true);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  switch (md) {
    case BOOT:
      showLogo();
      tft.loadFont(AA_FONT_LARGE);
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.drawString("MSL Payments", tft.width() / 2, tft.height() / 2 - 10);

      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.loadFont(AA_FONT_SMALL);
      tft.drawString(version, tft.width() / 2, tft.height() / 2 + 26);
      tft.drawString(__DATE__, tft.width() / 2, tft.height() / 2 + 42);
      tft.drawString(__TIME__, tft.width() / 2, tft.height() / 2 + 60);
      break;
    case WAITING_FOR_NTP:
      showLogo();
      tft.loadFont(AA_FONT_LARGE);
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.drawString("get time...", tft.width() / 2, tft.height() / 2 - 10);
      break;
    case FETCH_CA:
      showLogo();
      tft.loadFont(AA_FONT_LARGE);
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.drawString("security...", tft.width() / 2, tft.height() / 2 - 10);
      break;
    case REGISTER:
      showLogo();
      tft.loadFont(AA_FONT_LARGE);
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.drawString("registering...", tft.width() / 2, tft.height() / 2 - 10);
      break;
    case REGISTER_PRICELIST:
      showLogo();
      tft.loadFont(AA_FONT_LARGE);
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.drawString("pricelist...", tft.width() / 2, tft.height() / 2 - 10);
      break;
    case WAIT_FOR_REGISTER_SWIPE:
      showLogo();
      tft.loadFont(AA_FONT_LARGE);
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.drawString("new terminal", tft.width() / 2, tft.height() / 2 - 10);
      tft.loadFont(AA_FONT_MEDIUM);
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.drawString("swipe admin tag", tft.width() / 2, tft.height() / 2 + 20);
      break;
    case ENTER_AMOUNT:
      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.loadFont(AA_FONT_LARGE);
      if (NA) {
        tft.drawString("Swipe to pay", tft.width() / 2, 20);
        scrollpanel_loop();
      } else {
        tft.drawString("- no articles -", tft.width() / 2, 20);
      }
      break;
    case OK_OR_CANCEL:
      tft.loadFont(AA_FONT_SMALL);
      tft.drawString("cancel", tft.width() / 2 - 30, tft.height()  - 12);
      tft.drawString("OK", tft.width() / 2 + 48, tft.height()  - 12);

      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.loadFont(AA_FONT_LARGE);
      tft.drawString("PAY", tft.width() / 2, tft.height() / 2 - 52);

      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.drawString(amounts[amount], tft.width() / 2, tft.height() / 2 - 10);
      tft.loadFont(AA_FONT_SMALL);
      tft.drawString(prices[amount], tft.width() / 2, tft.height() / 2 + 15);
      tft.drawString(" ? ", tft.width() / 2, tft.height() / 2 + 35);
      break;
    case DID_CANCEL:
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.loadFont(AA_FONT_LARGE);
      tft.drawString("aborted", tft.width() / 2, tft.height() / 2 - 52);
      break;
    case DID_OK:
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.loadFont(AA_FONT_SMALL);
      tft.drawString("paying..", tft.width() / 2, tft.height() / 2 - 52);
      break;
  }
}

void updateClock(bool force) {
  static time_t last_now = 0;
  time_t now = time(nullptr);

  if (!force) {
    // only show the lock post NTP sync.
    if (now < 1000)
      return;

    if (last_now == now || now % 60 != 0 || now - last_now < 60)
      return;
  };
  last_now = now;

  char * p = ctime(&now);
  p += 11;
  p[5] = 0; // or use 8 to also show seconds.

  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.loadFont(AA_FONT_TINY);
  int padding = tft.textWidth(p, -1);
  tft.setTextPadding(padding);

  tft.drawString(p, tft.width(), 0);

  wifiIcon(0, 0);
  return;
  tft.setTextDatum(TL_DATUM);
  char str[128];
  snprintf(str, sizeof(str), "%d Kb %d dBm", (512 + heap_caps_get_free_size(MALLOC_CAP_INTERNAL)) / 1024UL, (int)WiFi.RSSI());
  tft.drawString(str, 0, 0);
};

void updateDisplay_progressText(const char * str) {
  Log.println(str);
  // updateDisplay();
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  tft.loadFont(AA_FONT_MEDIUM);
  int padding = tft.textWidth(str, -1);
  tft.setTextPadding(padding);

  tft.drawString(str, tft.width() / 2,  tft.height() - 20);
}

void updateDisplay_warningText(const char * str) {
  Log.println(str);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  showLogo();
  tft.setTextDatum(MC_DATUM);
  tft.loadFont(AA_FONT_MEDIUM);
  tft.drawString(str, tft.width() / 2,  tft.height() - 20);
}

void displayForceShowErrorModal(const char * str, const char * substr) {
  tft.fillScreen(TFT_BLACK);
  updateClock(true);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);

  tft.loadFont(AA_FONT_LARGE);
  tft.drawString("ERROR", tft.width() / 2, tft.height() / 2 - 22);
  tft.loadFont(AA_FONT_SMALL);
  tft.drawString(str, tft.width() / 2, tft.height() / 2 + 2);
  tft.drawString((substr && strlen(substr)) ? substr : "resetting...", tft.width() / 2, tft.height() / 2 +  32);
  Log.printf("Error: %s -- resetting\n", str);
  delay(1500);
}

void displayForceShow(const char * str, const char * substr) {
  tft.fillScreen(TFT_BLACK);
  showLogo();
  updateClock(true);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);

  if (str) {
    tft.loadFont(AA_FONT_LARGE);
    tft.drawString(str, tft.width() / 2, tft.height() / 2 + 2);
  };
  if (substr) {
    tft.loadFont(AA_FONT_SMALL);
    tft.drawString(substr, tft.width() / 2, tft.height() / 2 - 20);
  };
}

void displayForceShowModal(const char * str, const char * substr) {
  displayForceShow(str, substr);
  delay(1500);
}

void setTFTPower(bool onoff) {
  Log.println(onoff ? "Powering display on" : "Powering display off");

#ifdef  TFT_BL
  if (!onoff) digitalWrite(TFT_BL, onoff ? TFT_BACKLIGHT_ON : (!TFT_BACKLIGHT_ON));
#endif

#ifdef ST7735_DISPON
  tft.writecommand(onoff ? ST7735_DISPON : ST7735_DISPOFF);
#else
#ifdef ST7789_DISPON
  tft.writecommand(onoff ? ST7789_DISPON : ST7789_DISPOFF);
#else
#error "No onoff driver for this TFT screen"
#endif
#endif

  delay(100);
#ifdef  TFT_BL
  if (onoff) digitalWrite(TFT_BL, onoff ? TFT_BACKLIGHT_ON : (!TFT_BACKLIGHT_ON));
#endif
}
