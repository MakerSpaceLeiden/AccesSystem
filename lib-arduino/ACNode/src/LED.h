#pragma once
#include "PowerNodeV11.h"
#include "Ticker.h"

class LED {
public:
   typedef enum { LED_OFF, LED_FLASH, LED_SLOW, LED_FAST, LED_ERROR, LED_PENDING, LED_IDLE, LED_ON, NEVERSET } led_state_t;

   LED(const byte pin = AART_LED);

   void set(led_state_t state);

private:
   const byte _pin; 
   Ticker _ticker;
   led_state_t _lastState;
};


