
const char *ledstateName[ NEVERSET ] = { "off", "flash", "slow", "fast", "on" };

void flipPin(uint8_t pin) {
  static unsigned int tock = 0;
  if (pin & 128) {
    digitalWrite(pin & 127, !(tock & 31));
  } else {
    digitalWrite(pin, !digitalRead(pin));
  }
  tock++;
}

void setLED(Ticker & t, uint8_t pin, int state) {
  switch ((LEDstate) state) {
    case LED_OFF:
      t.detach();
      digitalWrite(pin, 0);
      break;
    case LED_FLASH:
      pin |= 128;
      t.attach_ms(100, flipPin,  pin); // no need to detach - code will disarm and re-use existing timer.
      break;
    case LED_SLOW:
      digitalWrite(pin, 1);
      t.attach_ms(500, flipPin, pin); // no need to detach - code will disarm and re-use existing timer.
      break;
    case LED_FAST:
      t.attach_ms(100, flipPin, pin); // no need to detach - code will disarm and re-use existing timer.
      break;
    case LED_ON:
      t.detach();
      digitalWrite(pin, 1);
      break;
  }
}

// Note: we're using an int rather than a LEDstate -- as the latter
// is not liked by Arduino its magic 'header detect' logic (1.5).
//
Ticker greenLEDTicker;
void setGreenLED(int state) {
  static LEDstate lastgreen = NEVERSET;
  if (lastgreen != state)
    setLED(greenLEDTicker, LED_GREEN, state);
  lastgreen = (LEDstate) state;
}

Ticker orangeLEDTicker;
void setOrangeLED(int state) {
  static LEDstate lastorange = NEVERSET;
  if (lastorange != state)
    setLED(orangeLEDTicker, LED_ORANGE, state);
  lastorange = (LEDstate) state;
}
