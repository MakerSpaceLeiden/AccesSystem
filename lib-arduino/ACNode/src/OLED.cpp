/* Quick hack to drive a NICONS8x64 display.

    setText(...         set scrolling text
    setIcon(slot,icon   0....11 (NICONS slots) of 8x8 icons in the top row.

    OLED scr = OLED();

    src.setup();
    src.setTest("Hello World");

    src.oled_loop(); // in oled_loop.

    Defined icons:
    persun -- small person
    lamp
    machine

*/
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Fonts/FreeSans24pt7b.h>
#include <Fonts/FreeSans9pt7b.h>

#include <OLED.h>

const char * OLED::name() { return "OLED"; }

void OLED::begin( const uint8_t i2c_addr, TwoWire * i2cbus) 
{
      _display = new Adafruit_SSD1306(_width, _height, i2cbus, -1 /* no reset */);
      _display->begin(SSD1306_SWITCHCAPVCC, i2c_addr);
      for (int i = 0; i < NICONS; i++) _icons[i] = NULL;
}

void OLED::report(JsonObject& report) {
     report["oled_text"] = buff; 
    }

void OLED::setSpeed(int speed) {
      _speed = speed;
    }

void OLED::setIcon(int slot, const unsigned char *icon) {
      if (slot > 0 && slot <= 9)
        _icons[slot] = icon;

      oled_loop(true);
    }

void OLED::setText(const char * s) {
      strncpy(buff, s, sizeof(buff));

      _display->setTextSize(1);      // Normal 1:1 pixel scale
      _display->setFont(&FreeSans24pt7b);
      _display->setTextWrap(false);

      int16_t bbx_text_x, bbx_text_y;
      uint16_t h;
      _display->getTextBounds(buff, 0, 0, &bbx_text_x, &bbx_text_y, &w, &h);
      x = 0;
      oled_loop(true);
    }

void OLED::operator=(const char * str) {
      setText(str);
    }

void OLED::oled_loop(bool force) {
      int dx =  (millis() - _last) * _speed / 1000;

      if (!force && dx < 1)
        return;

      x += dx;
      _last += dx * 1000 / OLED_DEFAULT_SPEED;

      if (x > w) x = -_width;

      _display->clearDisplay();

      _display->setTextColor(WHITE); // Draw white text
      _display->setTextSize(1);      // Normal 1:1 pixel scale
      _display->setFont(&FreeSans24pt7b);
      _display->setTextWrap(false);

      _display->setCursor(-x, y);    // Start at top-left corner
      _display->print(buff);
#if 0
      _display->setFont(&FreeSans9pt7b);
      _display->setCursor(0, _height- 2);
      _display->print("P7 L2 M3");
#endif
      for (int i = 0; i < NICONS; i++) {
        if (_icons[i])
          _display->drawBitmap(4 + x * 10, 0, _icons[i], 8, 8, 1);
      }
      _display->display();

    }
