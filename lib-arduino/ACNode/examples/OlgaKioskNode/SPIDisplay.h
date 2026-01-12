#include <Wire.h>

#include <Adafruit_GFX.h>  // Core graphics library
#include <Adafruit_I2CDevice.h>
#include <Adafruit_ST7789.h>  // Hardware-specific library for ST7789
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSans12pt7b.h>


#ifndef ST77XX_DARKGREEN
#define ST77XX_DARKGREEN (0x03E0)
#endif

#define HW_SPI

#ifdef HW_SPI
SPIClass spi(HSPI);
Adafruit_ST7789 tft = Adafruit_ST7789(&spi, OLED_CS, OLED_DC_RS, OLED_RST);
#else
Adafruit_ST7789 tft = Adafruit_ST7789(OLED_CS, OLED_DC_RS, OLED_MOSI, OLED_CLK, OLED_RST);
#endif

const int SCREEN_WIDTH = 240;
const int SCREEN_HEIHGT = 135;

void setupDisplay() {
#ifdef HW_SPI
  spi.begin(OLED_CLK, -1, OLED_MOSI, OLED_CS);
#endif

  tft.init(SCREEN_HEIHGT, SCREEN_WIDTH);  // Swapped as we're rotating the screen 90 degrees.
  tft.setRotation(3);

  tft.fillScreen(ST77XX_WHITE);
  tft.setTextColor(ST77XX_BLACK);
  tft.setTextWrap(true);
  tft.setFont(NULL);
  tft.setCursor(0, 0);

  tft.print("FW: " __DATE__ " " __TIME__ "\n");

  tft.setFont(&FreeSansBold18pt7b);
}

void updateStatusBar(const char* str) {
  tft.setFont(NULL);
  tft.setCursor(0, 0);
  tft.setTextColor(ST77XX_BLACK);
  tft.fillRect(0,0,tft.width()-1,8, ST77XX_WHITE);
  tft.print(str);
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
  tft.setCursor((tft.width() - w) / 2, (tft.height()) / 2 + h / 2 - 4);
  tft.print(string);
  Debug.printf("TFT: %s\n", string);
}

void centeredText(const char* string, uint16_t col) {
  // tft.fillScreen(ST77XX_WHITE);
  tft.fillRect(0,8,tft.width()-1,tft.height()-9, ST77XX_WHITE);
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

  static char buff[60] = "";
  static unsigned long lst = 0;
  if ((millis() - lst) < 1000)
    return;
  lst = millis();

  static time_t st = 0;
  time_t now = time(NULL);
  char* p = (char*)"--:--";
  unsigned int h = millis() / 1000;
  char u = 's';

  if (now > 10 * 24 * 3600) {
    //  0123456789012345678 9 0
    // "Thu Nov  4 09:47:43\n\0" -> 09:47\0
    p = ctime(&now);
    p += 11;
    p[strlen(p) - 9] = 0;  // remove CRL/LF and seconds.
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


  // tft.fillRect(0, tft.height() - 8 - 1, tft.width(), 8, ST77XX_WHITE);

  // This type of display is very slow to redraw; so we do it char by char
  // to avoid too much flicker.
  //
  static char newbuff[60];
  size_t fql = strlen(WiFi.localIP().toString().c_str());

  char s[16] = "               ";
  if (fql < 15)
    s[15 - fql] = '\0';

  snprintf(newbuff, sizeof(newbuff), "http://%s/login%s %3u%c %s", WiFi.localIP().toString().c_str(), s, h, u, p);

  tft.setFont(NULL);
  uint16_t y = tft.height() - 9;
  uint16_t x = 2;
  for (int i = 0; i < strlen(buff) && i < strlen(newbuff) && x < tft.width(); i++) {
    // if (buff[i] != newbuff[i])
    {
      // wipe old (or we could use the XOR trick here ??)
      tft.setCursor(x, y);
      tft.setTextColor(ST77XX_WHITE);
      tft.print(buff[i]);

      // paint new
      tft.setCursor(x, y);
      tft.setTextColor(ST77XX_BLACK);
      tft.print(newbuff[i]);
    };
    // next char - fixed width font.
    x += 6;
  }
  strncpy(buff, newbuff, sizeof(buff));
}

void loopDisplay() {
  bottomStatusLine();
  tocker();
}
