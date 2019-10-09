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
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include <ACBase.h>

#define OLED_DEFAULT_SPEED        144 // pixels/second

#define OLED_DEFAULT_SCREEN_WIDTH 128
#define OLED_DEFAULT_SCREEN_HEIGHT 64
#define OLED_DEFAULT_SCREEN_I2C_ADDR 0x3C

#define NICONS (12)

class OLED : public ACBase {
  private:
    int _speed = OLED_DEFAULT_SPEED;
    int16_t x = 0;
    int16_t y = 52; // slighty above the middle - as G's stick out.
    uint16_t w = 0;
    unsigned long _last = 0;
    const unsigned char * _icons[ NICONS ];
    char buff[128];
    Adafruit_SSD1306 * _display;
    const uint8_t _width,_height;
    void oled_loop(bool force = false);

  public:
    const char * name();
    OLED(
	const uint8_t width = OLED_DEFAULT_SCREEN_WIDTH, 
	const uint8_t height = OLED_DEFAULT_SCREEN_WIDTH
	) : _width(width), _height(height) {};

    void begin(
	const uint8_t i2c_addr = OLED_DEFAULT_SCREEN_I2C_ADDR, 
	TwoWire * i2cbus= &Wire
    );

    void report(JsonObject& report);

    void setSpeed(int speed);
    void setIcon(int slot, const unsigned char *icon);

    void setText(const char * s);
    void operator=(const char * str);
};
