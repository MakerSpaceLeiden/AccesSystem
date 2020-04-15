
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

// Note - something goes screwwy with the Arduino incluide magic if we use
// LEDstate here. So we use a safe int and cast.
//
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


void LED::begin(const char * _name, uint8_t _pin) {
  ticker = Ticker();
  state = NEVERSET;
  pin = _pin;
  name = _name;
  digitalWrite(pin, 0);
  pinMode(pin, OUTPUT);
}

void LED::operator=(LEDstate newState)  {
  setState(newState);
}

void LED::setState(LEDstate newState) {
  if (state == newState)
    return;
  Debug.printf("The %s LED changed state %s to %s.\n",
               name, ledstateName[state], ledstateName[newState]);
  state = newState;

  // Note - something goes screwwy with the Arduino incluide magic if we use
  // LEDstate here. So we use a safe int and cast.
  //
  setLED(ticker, pin, (int) state);
}
