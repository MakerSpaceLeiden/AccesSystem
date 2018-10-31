#pragma once
#include "PowerboardV1.1.h"
#include "Ticker.h"

class LED {
public:
   typedef enum { LED_OFF, LED_FLASH, LED_SLOW, LED_FAST, LED_PENDING, LED_IDLE, LED_ON, NEVERSET } led_state_t;

   LED(const byte pin = AART_LED);

   void setLed(int state);

private:
   const byte _pin; 
   Ticker _ticker;
}

