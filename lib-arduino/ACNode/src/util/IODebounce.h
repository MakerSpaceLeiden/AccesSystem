/*
  ButtonDebounce.h - Library for Button Debounce.
  Created by Maykon L. Capellari, September 30, 2017.
  Released into the public domain.
*/
#ifndef IODebounce_h
#define IODebounce_h

#include "Arduino.h"
#include <functional>

#include "ExpandedGPIO.h"
#include "ACBase.h"

class IODebounce : public ACBase {
  public:
    IODebounce(const char *name = NULL, int pin = -1, unsigned long delay = 40 /* mSeconds stable */);
    ~IODebounce();

    // const char * name() { return "IODebounce"; };
    
    void setAnalogThreshold(unsigned short val); // Set to 0 to go back to digital again.

    bool state();
    bool rawState();
    unsigned short raw();

    typedef std::function<bool(const int)> digitalReadFunction;
    void setDigitalReadFunction(digitalReadFunction func) { _digitalRead = func; };

    typedef std::function<float(const int)> analogReadFunction;
    void setAnalogReadFunction(analogReadFunction func) { _analogRead = func; };

    typedef std::function<void(const int)> ButtonCallback;
    void setCallback(ButtonCallback,int mode = CHANGE);

    bool operator ==(int s) { return (s ? HIGH : LOW) == _lastStateBtn; };
    bool operator !=(int s) { return (s ? HIGH : LOW) != _lastStateBtn; };
 
    void _ticker_update();
    void loop();
  private:
    int _pin;
    int _mode; // Interrupt mode (RISING, FALLING, CHANGE, ONLOW, ONHIGH -- see Arduino.h)
    unsigned short _analogThreshold = 0;
    unsigned long _delay;
    unsigned long _lastChangeTime;
    bool _lastStateBtn, _prevStateBtn, _hasfired=false;
    ButtonCallback _callBack = NULL;
    digitalReadFunction _digitalRead = &expandedDigitalRead;
    analogReadFunction _analogRead = &expandedAnalogRead;
};
#endif
