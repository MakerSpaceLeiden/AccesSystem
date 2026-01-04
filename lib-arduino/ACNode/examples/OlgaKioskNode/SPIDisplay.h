#include <Wire.h>

#include <Adafruit_GFX.h>  // Core graphics library
#include <Adafruit_I2CDevice.h>
#include <Adafruit_ST7789.h>  // Hardware-specific library for ST7789
#include <Fonts/FreeSansBold18pt7b.h>


#ifndef ST77XX_DARKGREEN
#define ST77XX_DARKGREEN (0x03E0)
#endif

Adafruit_ST7789 tft = Adafruit_ST7789(OLED_CS, OLED_DC_RS, OLED_MOSI, OLED_CLK, OLED_RST);
const int SCREEN_WIDTH = 240;
const int SCREEN_HEIHGT = 135;

void setupDisplay() {

  tft.init(SCREEN_HEIHGT, SCREEN_WIDTH);  // Swapped as we're rotating the screen 90 degrees.
  tft.setRotation(3);

  tft.fillScreen(ST77XX_WHITE);
  tft.setTextColor(ST77XX_BLACK);
  tft.setTextWrap(true);
  tft.print("Started: " __DATE__ " " __TIME__ "\n");

  tft.setFont(&FreeSansBold18pt7b);
}

void updateProgressBar(float p) {
  unsigned short l = tft.width() - 48 - 4;
  unsigned short w = l * p;
  static unsigned short lastw = w + 1;
  if (w == lastw) return;
  tft.fillRect(20 + 2, tft.height() - 40 + 2, w, 20 - 4, ST77XX_DARKGREEN);
  lastw = w;
};

void printCentered(const char* string) {
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(string, 0, 0, &x1, &y1, &w, &h);
  tft.setCursor((tft.width() - w) / 2, (tft.height()) / 2 + h / 2);
  tft.print(string);
  Debug.printf("TFT: %s\n", string);
}

void centeredText(const char* string, uint16_t col) {
  tft.fillScreen(ST77XX_WHITE);
  tft.fillCircle(tft.width() / 2, tft.height() / 2, (tft.height() / 2) * 0.8, col);

  tft.setTextColor(ST77XX_WHITE);
  tft.setFont(&FreeSansBold18pt7b);
  printCentered(string);
}

void tocker() {
  static unsigned long lst = 0;
  if ((millis() - lst) < 500)
    return;
  lst = millis();

  static bool ping;
  ping = !ping;
  // tft.fillCircle(tft.width() - 5, tft.height() - 6, 3, ping ? ST77XX_WHITE : ST77XX_RED);
  tft.fillCircle(tft.width() - 1 - 4, 4, 3, ping ? ST77XX_WHITE : ST77XX_RED);
}

void bottomStatusLine() {
  static unsigned long lst = 0;
  if ((millis() - lst) < 1000)
    return;
  lst = millis();

  static time_t st = 0;
  time_t now = time(NULL);
  char* p = (char*)"--:--:--";
  unsigned int h = millis() / 1000;
  char u = 's';

  if (now > 10 * 24 * 3600) {
    //  0123456789012345678 9 0
    // "Thu Nov  4 09:47:43\n\0" -> 09:47\0
    p = ctime(&now);
    p += 11;
    p[strlen(p) - 6] = 0;  // remove CRL/LF
    if (!st) st = now - h;
    h = now - st;
    if (h > 300) {
      h /= 60;
      u = 'm';
    };
    if (h > 300) {
      h /= 60;
      u = 'h';
    };
    if (h > 300) {
      h /= 60;
      u = 'd';
    };
    if (h > 300) {
      h /= 60;
      u = 'm';
    };
  };

  char buff[60] = "";

  tft.fillRect(0, tft.height() - 8 - 1, tft.width(), 8, ST77XX_WHITE);

  tft.setFont(NULL);

  tft.setCursor(2, tft.height() - 9);
  tft.setTextColor(ST77XX_WHITE);
  tft.print(buff);

  snprintf(buff, sizeof(buff), "http://%s     %3u%c    %s", WiFi.localIP().toString().c_str(), h, u, p);


  tft.setCursor(2, tft.height() - 9);
  tft.setTextColor(ST77XX_BLACK);
  tft.print(buff);
}

void loopDisplay() {
  bottomStatusLine();
  tocker();
}
