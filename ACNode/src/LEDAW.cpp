#include "ACBaseNode.h"
#include "ExpandedGPIO.h"

#include "LEDAW.h"

void LEDAW::begin() {
    expandedPinMode(_pin,OUTPUT);
    set(LED_FAST);
}

void LEDAW::_set(bool on) {
    expandedDigitalWrite(_pin, on);
}
