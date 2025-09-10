#include "ACBaseNode.h"
#include "ExpandedGPIO.h"

#include "LED.h"

// We cannot quite call objects from the ticker callback; so
// we use a tiny bit of glue.
//
static void flipPin(LED * led) { led->_update(); }

LED::LED(const char * name, const byte pin, const bool inverted) : ACBase(name), _pin(pin) ,_inverted(inverted) {
#if 0
	// Temporarily removed - we rely on the loop() now
	// until the Wire/Adafruit issues on the unchecked
        // Semaphore return flag is fixed.
        if (_pin != -1) {
  	   _ticker = Ticker();
        };
#endif
	_lastState = NEVERSET;
};

void LED::begin() {
  	expandedPinMode(_pin, OUTPUT);
	set(LED_FAST);
}

void LED::_on() { _set(true); }
void LED::_off() { _set(false); }
void LED::_set(bool on) { expandedDigitalWrite(_pin, on ^ _inverted); }

//                       01234567012345670123456701234567
#define PATTERN_IDLE   0b10000000000000000000000000000000
#define PATTERN_EVEN   0b10101010101010101010101010101010
#define PATTERN_SO     0b11100111100111001001001000000000
#define PATTERN_A      0b11000000111111000000000000000000

void LED::_update() {
  bool on = false;
  unsigned int pattern = PATTERN_EVEN;

  if (_lastState == LED_IDLE)
    pattern = PATTERN_IDLE;

  on = pattern & (1<<(_tock & 31)); 
  _set(on);
  _tock++;
}

void LED::loop() {
   if (millis() - _lst < _tsSpeed || _tsSpeed == 0)
	return;
   _lst = millis();
   _update();
}

void LED::set(led_state_t state) {
  if (_lastState == state)
     return;
  _lastState = state;
  if(_pin == -1) {
      // Serial.printf("LED - change to state %d\n", state);
      return;
  }
  switch(state) {
    case LED_OFF:
      _tsSpeed = 0;
  //    _ticker.detach();
      _off();
      break;
    case LED_ON:
  //    _ticker.detach();
      _on();
      _tsSpeed = 0;
      break;
    case LED_FLASH:
    case LED_IDLE:
    case LED_PENDING:
    case LED_FAST:
      _tsSpeed = 500;
//      _ticker.attach_ms(_tsSpeed, &flipPin, this); // no need to detach - code will disarm and re-use existing timer.
      break;
    case LED_SLOW:
      _tsSpeed = 500;
////      _ticker.attach_ms(_tsSpeed, &flipPin,  this); // no need to detach - code will disarm and re-use existing timer.
      break;
    case LED_ERROR:
    case NEVERSET: // include this here - though it should enver happen. 50 hz flash
    default:
      _tsSpeed = 20;
////      _ticker.attach_ms(_tsSpeed, &flipPin, this); // no need to detach - code will disarm and re-use existing timer.
      break;
  }
}
