#include "Adafruit_NoOLED.h"
#if 0
/*!
    @brief  Constructor for bufer only version of the Adafruit drivers.
    @param  w
            Display width in pixels
    @param  h
            Display height in pixels
    @note   Call the object's begin() function before use -- buffer
            allocation is performed there!
*/
Adafruit_NoOLED::Adafruit_NoOLED(uint16_t w, uint16_t h, TwoWire *twi = &Wire)
    : Adafruit_GrayOLED(1, w, h, twi, -1, -1, -1);

// REFRESH DISPLAY ---------------------------------------------------------

/*!
    @brief  Push data currently in RAM to SH110X display.
    @note   Drawing operations are not visible until this function is
            called. Call after each graphics command, or after a whole set
            of graphics commands, as best needed by one's own application.
*/
void Adafruit_NoOLED::display(void) {
  window_x1 = 1024;
  window_y1 = 1024;
  window_x2 = -1;
  window_y2 = -1;
}
#endif

