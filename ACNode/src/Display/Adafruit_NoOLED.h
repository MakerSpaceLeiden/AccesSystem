#ifndef _Adafruit_NoOLED_H_
#define _Adafruit_NoOLED_H_
#if 0
#include <Adafruit_GrayOLED.h>

/// fit into the SH110X_ naming scheme
#define SH110X_BLACK 0   ///< Draw 'off' pixels
#define SH110X_WHITE 1   ///< Draw 'on' pixels
#define SH110X_INVERSE 2 ///< Invert pixels

class Adafruit_NoOLED: public Adafruit_GrayOLED {
public:
  Adafruit_NoOLED(uint16_t w, uint16_t h, TwoWire * wire);
  void display(void);
};
#endif
#endif
