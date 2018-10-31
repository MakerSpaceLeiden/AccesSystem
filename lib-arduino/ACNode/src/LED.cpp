#include <ACNode.h>
#include "LEDs.h"

const char *ledstateName[ NEVERSET ] = { "off", "flash", "slow", "fast", "on" };

void flipPin(unsigned char pin) {
  static unsigned int tock = 0;
  if (pin & 128) {
    digitalWrite(pin & 127, !(tock & 31));
  } else {
    digitalWrite(pin, !digitalRead(pin));
  }
  tock++;
}

void LED::LED(const byte pin) {
	_pin = pin;
	pinMode(_pin, OUTPUT);
  	_ticker = Ticker();
	setLed(LED_FAST);
}

void LED::setLED(led_state_t state, unsigned char pin) {
  switch ((LEDstate) state) {
    case LED_OFF:
      _ticker.detach();
      digitalWrite(pin, 0);
      break;
    case LED_ON:
      _ticker.detach();
      digitalWrite(pin, 1);
      break;
    case LED_FLASH:
      _ticker.attach_ms(100, &flipPin,  128 | pin); // no need to detach - code will disarm and re-use existing timer.
      break;
    case LED_IDLE:
    case LED_SLOW:
      digitalWrite(pin, 1);
      _ticker.attach_ms(500, &flipPin, pin); // no need to detach - code will disarm and re-use existing timer.
      break;
    case LED_PENDING:
    case LED_FAST:
      _ticker.attach_ms(100, &flipPin, pin); // no need to detach - code will disarm and re-use existing timer.
      break;
    case NEVERSET: // include this here - though it should enver happen. 50 hz flash
      _ticker.attach_ms(20, &flipPin, pin); // no need to detach - code will disarm and re-use existing timer.
      break;
    // we have no default - as to get a compiler warning on missing entry.
  }
}

