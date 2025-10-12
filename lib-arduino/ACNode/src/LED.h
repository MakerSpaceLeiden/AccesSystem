#pragma once
#include "ACBase.h"

class LED : public ACBase {
public:
   typedef enum { LED_OFF, LED_FLASH, LED_SLOW, LED_FAST, LED_ERROR, LED_PENDING, LED_IDLE, LED_ON, NEVERSET } led_state_t;

   LED(const char *name = NULL, const byte pin = -1, bool inverted = false);

   void set(led_state_t state);

   // Not really public - but needed in the ticker callbacks.
   void _on();
   void _off();
   virtual void _set(bool on);
   void _update();

   void begin();
   void loop();

protected:
   unsigned int _pin,_tock;
   const bool _inverted;
   led_state_t _lastState;
   unsigned long _lst, _tsSpeed;
private:
};

