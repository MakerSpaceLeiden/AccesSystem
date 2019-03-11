/* Quick hack to drive a 128x64 display.

    setText(...         set scrolling text
    setIcon(slot,icon   0....11 (12 slots) of 8x8 icons in the top row.

    OLED scr = OLED();

    src.setup();
    src.setTest("Hello World");

    src.loop(); // in loop.

    Defined icons:
    persun -- small person
    lamp
    machine
    
*/
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include <Fonts/FreeSans24pt7b.h>
#include <Fonts/FreeSans9pt7b.h>

static const unsigned char PROGMEM persun[] = {
  B00111000,
  B00100100,
  B00100100,
  B00011000,
  B01111110,
  B10011001,
  B00011000,
  B00100100,
  B01100110,
};

static const unsigned char PROGMEM lamp[] = {
  B00111100,
  B01000010,
  B01000010,
  B01000010,
  B00111100,
  B00100100,
  B00100100,
  B00011000,
};

static const unsigned char PROGMEM machine[] = {
  B11110000,
  B11110000,
  B11110000,
  B11110000,
  B01110000,
  B00110000,
  B00010000,
  B11111111,
};


#define SPEED         72 // pixels/second
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

class OLED {
  private:
    const uint8_t oled_sd1306_i2c_addr =  0x3C;
    int _speed = SPEED;
    int16_t x = 0;
    int16_t y = 52; // slighty above the middle - as G's stick out.
    uint16_t w = 0;
    unsigned long _last = 0;
    const unsigned char * _icons[ 12 ];
    char buff[128];
    Adafruit_SSD1306 * _display;

  public:
    OLED() {};

    void setup() {
      _display = new Adafruit_SSD1306(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1 /* no reset */);
      _display->begin(SSD1306_SWITCHCAPVCC, oled_sd1306_i2c_addr);
      for (int i = 0; i < 12; i++) _icons[i] = NULL;
    }

    void setSpeed(int speed) {
      _speed = speed;
    }
    void setIcon(int slot, const unsigned char *icon) {
      if (slot > 0 && slot <= 9)
        _icons[slot] = icon;

      loop(true);
    }

    void setText(const char * s) {
      strncpy(buff, s, sizeof(buff));

      _display->setTextSize(1);      // Normal 1:1 pixel scale
      _display->setFont(&FreeSans24pt7b);
      _display->setTextWrap(false);

      int16_t bbx_text_x, bbx_text_y;
      uint16_t h;
      _display->getTextBounds(buff, 0, 0, &bbx_text_x, &bbx_text_y, &w, &h);
      x = 0;
      loop(true);
    }

    void loop(bool force = false) {
      if (!force && (millis() - _last < (1000 / SPEED)))
        return;

      if (x > w) x = -SCREEN_WIDTH;

      _display->clearDisplay();

      _display->setTextColor(WHITE); // Draw white text
      _display->setTextSize(1);      // Normal 1:1 pixel scale
      _display->setFont(&FreeSans24pt7b);
      _display->setTextWrap(false);

      _display->setCursor(-x, y);    // Start at top-left corner
      _display->print(buff);
#if 0
      _display->setFont(&FreeSans9pt7b);
      _display->setCursor(0, SCREEN_HEIGHT - 2);
      _display->print("P7 L2 M3");
#endif
      for (int i = 0; i < 12; i++) {
        if (_icons[i])
          _display->drawBitmap(4 + x * 10, 0, _icons[i], 8, 8, 1);
      }
      _display->display();

      int dx =  (millis() - _last) * _speed / 1000;
      x += dx;
      _last += dx * 1000 / SPEED;
    }
};


