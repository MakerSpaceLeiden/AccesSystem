#pragma once
#include "PowerNodeV11.h"
#include "Ticker.h"

class LED {
public:
   typedef enum { LED_OFF, LED_FLASH, LED_SLOW, LED_FAST, LED_ERROR, LED_PENDING, LED_IDLE, LED_ON, NEVERSET } led_state_t;

   LED(const byte pin = AART_LED, bool inverted = false);

   void set(led_state_t state);

private:
   unsigned int _pin; 
   const bool _inverted;
   Ticker _ticker;
   led_state_t _lastState;
};


