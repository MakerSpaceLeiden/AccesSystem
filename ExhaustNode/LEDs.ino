Ticker greenLEDTicker;
Ticker orangeLEDTicker;

const char *ledstateName[ NEVERSET ] = { "off", "slow", "fast", "on" };
LEDstate lastorange = NEVERSET;
LEDstate lastgreen = NEVERSET;

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

void setGreenLED(int state) {
  if (lastgreen != state)
    setLED(greenLEDTicker, LED_GREEN, state);
  lastgreen = (LEDstate) state;
}
void setOrangeLED(int state) {
  if (lastorange != state)
    setLED(orangeLEDTicker, LED_ORANGE, state);
  lastorange = (LEDstate) state;
}
