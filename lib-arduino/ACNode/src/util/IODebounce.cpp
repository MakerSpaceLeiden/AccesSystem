#include "util/IODebounce.h"
#define SAMPLES_PER_DELAY (4)
#include <TLog.h>

#if 0
static void _update(uint32_t arg) {
    IODebounce * c = (IODebounce*)arg;
    c->_ticker_update();
}
#endif

IODebounce::IODebounce(const char *name, int pin, unsigned long delay) : ACBase(name) {
    _pin = pin;
    _delay = delay;
    _lastChangeTime = 0;
    _analogThreshold = 0;
    _prevStateBtn = _lastStateBtn = rawState();
#if 0
    // temporary removed - to see if we can solve the issue
    // with the broken Wire semaphore concept (see ticketXX)
    //
    _ticker = new Ticker();
    _ticker->attach_ms(delay/SAMPLES_PER_DELAY,_update,(uint32_t )this);
#endif
}

IODebounce::~IODebounce() {
#if 0
    delete _ticker;
#endif
};

void IODebounce::setAnalogThreshold(unsigned short val) {
    _analogThreshold = val;
};

bool IODebounce::rawState() {
    int btnState;
    if (_analogThreshold)
        btnState = this->_analogRead(_pin) > _analogThreshold ? HIGH : LOW;
    else
        btnState = this->_digitalRead(_pin) ? HIGH : LOW;
    return btnState;
}

unsigned short IODebounce::raw() {
    if (_analogThreshold)
        return this->_analogRead(_pin);
    return this->_digitalRead(_pin);
}; 

bool IODebounce::state(){
    return _lastStateBtn;
}

void IODebounce::status(JsonObject & report) {
   JsonObject m = report["io"].to<JsonObject>();
   JsonObject n = m[_name].to<JsonObject>();

   n["label"] = _name;
   n["state"] = _lastStateBtn;

   n["milliSecondsInThisState"] = millis() - _lastChangeTime;

   n["rawState"] = rawState();
   n["rawValue"] = raw();

   n["type"] =  _analogThreshold ? "ANALOG" : "DIGITAL";
   if (_analogThreshold) 
	n["analogThreshold"] = _analogThreshold;
};

void IODebounce::_ticker_update(){
    int btnState = rawState();
    
    if(btnState != _prevStateBtn) {
        _lastChangeTime = millis();
        _prevStateBtn = btnState;
        return;
    };
    
    if (_lastStateBtn == btnState)
        return; // no change
    
    if (millis() - _lastChangeTime < _delay)
        return;
    
    // We are longer than delay in a stable state that
    // differs from our last state; so we can consider
    // it suitably debounced now.
    //
    _prevStateBtn =  _lastStateBtn = btnState;
    _hasfired = true;
};

void IODebounce::loop() {
    _ticker_update();

    if (!_hasfired)
        return;

    _hasfired = false;

    if (!this->_callBack)
        return;
    
    if  (
         (_mode == CHANGE) ||
         ((_mode == ONLOW   || _mode == FALLING) && _lastStateBtn == LOW) ||
         ((_mode == ONHIGH  || _mode == RISING) && _lastStateBtn == HIGH)
         )
        this->_callBack(_lastStateBtn);
}

void IODebounce::setCallback(IOButtonCallback callback, change_t mode) {
    this->_callBack = callback;
    this->_mode = mode;
}
