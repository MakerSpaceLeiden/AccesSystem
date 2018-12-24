#include <ACNode-private.h>
#include "LED.h"

void flipPin(unsigned int pin) {
  static unsigned int tock = 0;
  bool onoff;

  // Sequencer or toggler
  if (pin & 128) 
    onoff =  (tock & 31) == 0; // one flash every 32 tocks.
  else
    onoff = digitalRead(pin & 127) == LOW;

  if (pin & 256)
    onoff = !onoff;

  digitalWrite(pin & 127,onoff ? HIGH : LOW);

  tock++;
}

LED::LED(const byte pin, const bool inverted) : _pin(pin) ,_inverted(inverted) {
	pinMode(_pin & 127, OUTPUT);
  	_ticker = Ticker();
	_lastState = NEVERSET;
	set(LED_FAST);
	if (_inverted)
		_pin &= 256;
}

void LED::set(led_state_t state) {
  if (_lastState == state)
     return;
  _lastState = state;
  switch(state) {
    case LED_OFF:
      _ticker.detach();
      digitalWrite(_pin & 127, 0 ^ _inverted);
      break;
    case LED_ON:
      _ticker.detach();
      digitalWrite(_pin & 127, 1 ^ _inverted);
      break;
    case LED_FLASH:
    case LED_IDLE:
      _ticker.attach_ms(100, &flipPin,  128 | _pin); // no need to detach - code will disarm and re-use existing timer.
      break;
    case LED_SLOW:
      digitalWrite(_pin & 127, 1 ^ _inverted);
      _ticker.attach_ms(500, &flipPin,  _pin); // no need to detach - code will disarm and re-use existing timer.
      break;
    case LED_PENDING:
    case LED_FAST:
      _ticker.attach_ms(100, &flipPin, _pin); // no need to detach - code will disarm and re-use existing timer.
      break;
    case LED_ERROR:
    case NEVERSET: // include this here - though it should enver happen. 50 hz flash
    default:
      _ticker.attach_ms(20, &flipPin, _pin); // no need to detach - code will disarm and re-use existing timer.
      break;
  }
}

