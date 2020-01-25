#pragma once

#include <Ticker.h>

typedef enum { LED_OFF, LED_FLASH, LED_SLOW, LED_FAST, LED_ON, NEVERSET } LEDstate;


class LED {
  public:
    // typedef enum { LED_OFF, LED_FLASH, LED_SLOW, LED_FAST, LED_ON, NEVERSET } LEDstate;
    void begin(const char * name, uint8_t pin);
      void operator=(LEDstate state);
    void setState(LEDstate state);
  private:
    const char * name;
    uint8_t pin;
    Ticker ticker;
    LEDstate state;
};


