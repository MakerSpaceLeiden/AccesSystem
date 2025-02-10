#include "util/IODebounce.h"
#define SAMPLES_PER_DELAY (4)
#include <TLog.h>

static void _update(uint32_t arg) {
    IODebounce * c = (IODebounce*)arg;
    c->_ticker_update();
}

IODebounce::IODebounce(int pin, unsigned long delay){
    _pin = pin;
    _delay = delay;
    _lastChangeTime = 0;
    _analogThreshold = 0;
    _prevStateBtn = _lastStateBtn = rawState();
    _ticker = new Ticker();
    _ticker->attach_ms(delay/SAMPLES_PER_DELAY,_update,(uint32_t )this);
}

IODebounce::~IODebounce() {
    delete _ticker;
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

void IODebounce::setCallback(ButtonCallback callback, int mode) {
    this->_callBack = callback;
    this->_mode = mode;
}
