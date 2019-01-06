#pragma once
#include "Ticker.h"

class LED {
public:
   typedef enum { LED_OFF, LED_FLASH, LED_SLOW, LED_FAST, LED_ERROR, LED_PENDING, LED_IDLE, LED_ON, NEVERSET } led_state_t;

   LED(const byte pin = -1, bool inverted = false);

   void set(led_state_t state);

   // Not really public - but needed in the ticker callbacks.
   void _on();
   void _off();
   void _set(bool on);
   void _update();

private:
   unsigned int _pin,_tock;
   const bool _inverted;
   Ticker _ticker;
   led_state_t _lastState;
};


