#pragma once

#include "BlackNodev111.h"
#include "util/cufflink_heartbeat.h"

BlackNodev111::BlackNodev111(const char * machine, const char * ssid, const char * ssid_passwd, acnode_proto_t proto)
: WhiteNodev108(machine, ssid, ssid_passwd, proto)  { CONSTS(); pop(); Serial.println("constructor done"); };

BlackNodev111::BlackNodev111(const char * machine, bool wired, acnode_proto_t proto )
: WhiteNodev108(machine,wired,proto) { CONSTS(); pop(); Serial.println("constructor done"); };

void BlackNodev111::pop() {
};

void BlackNodev111::begin() {
    Serial.println("BlackNodev111::begin()\n");

    // Starting with v1.11 - non core I/O is provided by an i2c IO/Expander.
    //
    ExpandedGPIO::getInstance().addAW9523();
    
    // Reduce the current to a sensible level.
    // Awaiting https://github.com/adafruit/Adafruit_AW9523/pull/5.
    //
    Wire.beginTransmission(0x58);
    Wire.write(0x11);
    Wire.write(3);
    Wire.endTransmission();
    
    xpinMode(LEDA,AW9523_LED_MODE);
    xanalogWrite(LEDA,0);
    
    xpinMode(LEDB,AW9523_LED_MODE);
    xanalogWrite(LEDB,0);
    
    xpinMode(LEDC,AW9523_LED_MODE);
    xanalogWrite(LEDC,0);
    
    xpinMode(LEDD,AW9523_LED_MODE);
    xanalogWrite(LEDD,0);
    
    xpinMode(LEDE,AW9523_LED_MODE);
    xanalogWrite(LEDE,0);
    
    xpinMode(OPTO0, INPUT);
    xpinMode(OPTO1, INPUT);
    xpinMode(OPTO2, INPUT);
    xpinMode(OPTO3, INPUT);

    yesButton = new IODebounce(YES_BUTTON);
    yesButton->setCallback([&](const int newState) {
        Debug.printf("YES button %s @ %s\n",newState ? "released" : "pressed", machinestate.label());

        if (_yesCallBack &&
            (_yesCallBackMode == CHANGE ||
             (newState && (_yesCallBackMode == ONHIGH || _yesCallBackMode == RISING)) ||
             (!newState &&(_yesCallBackMode == ONLOW || _yesCallBackMode == FALLING))
             ))
            if (_yesCallBack(newState))
                return;

        if( machinestate == SCREENSAVER) {
            machinestate = MachineState::WAITINGFORCARD;
            Debug.println("Switching off the screensaver");
            return;
        };
        
    });
    addHandler(yesButton);

    Serial.println("BlackNodev111::begin() done - onto super\n");
    super::begin();
    Serial.println("BlackNodev111::begin() - super done\n");
}

void BlackNodev111::setMonitoredOutput(uint8_t num, bool val) {
    if (num == OUT0)
        expectOut1 = val ? HIGH : LOW;
    if (num == OUT1)
        expectOut2 = val ? HIGH : LOW;
    xdigitalWrite(num,val);
}
                           
bool BlackNodev111::getMonitoredOutput(uint8_t num) {
    xpinMode(num,INPUT);
    bool out = digitalRead(num);
    xpinMode(num,OUTPUT);
    return out;
}

bool BlackNodev111::monitoredOutputIsOK(uint8_t num) {
    bool expect = (num == OUT0) ? expectOut1 : expectOut2;

    xpinMode(num,INPUT);
    bool curr = xdigitalRead(num);
    xpinMode(num,OUTPUT);
    
    return expect == curr;
}

void BlackNodev111::setYesCallback(ButtonCallback callback, int mode ) {
    _yesCallBack = callback;
    _yesCallBackMode = mode;
}
    
void BlackNodev111::loop() {
    super::loop();
    xanalogWrite(LEDE,hearthbeat());

    static unsigned last = millis();
    if (millis() - last < 10*1000)
        return;
        
    // Quite hardware specific; the relay can only be forced 'on' - either by a GPIO or
    // by a switch. It cannot be forced off. So we can only sensibly detect an 'illegal' on;
    // while it was expected to be off. Not sure if this reliable / does not cause glitches.
    //
    if (expectOut1 == LOW) {
        static unsigned long lst = 0;
        if (!monitoredOutputIsOK(OUT0)){
            if (lst == 0 || millis() - lst > 60 * 1000) {
                Log.printf("Warning - Output 1 measured as %s at hardware level; it should be %s.\n",
                           getMonitoredOutput(OUT0) ? "HIGH" : "LOW", expectOut1  ? "HIGH" : "LOW");
                lst = millis();
            };
            errorLed->set(LED::LED_FAST);
            xanalogWrite(LEDA,255);
        } else lst = 0;
    };

#if 0
    // Not yet reliable on out2, out1 is fine. We need to change
    // this to a proper extra pin from the AW for both cases.
    if (expectOut2 == LOW) {
        static unsigned long lst = 0;
        if (!monitoredOutputIsOK(OUT1)) {
            if (lst == 0 || millis() - lst > 60 * 1000) {
                Log.printf("Warning - Output 2 measured as %s at hardware level; it should be %s.\n",
                           getMonitoredOutput(OUT1) ? "HIGH" : "LOW", expectOut2  ? "HIGH" : "LOW");
                lst = millis();
            };
            errorLed->set(LED::LED_FAST);
            xanalogWrite(LEDA,255);
        } else lst = 0;
    };
#endif
}
