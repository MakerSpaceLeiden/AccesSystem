#include "ACBaseNode.h"
#include "ExpandedGPIO.h"

#include "LED.h"

// Pretty much identical to a normal LED - but under analog control
// as that is what the current mgnt of the AW chip requires.
//
class LEDAW : public LED {
public:
    LEDAW(const char *name, const byte pin = -1, bool inverted = false) : LED(name, pin,inverted) {};
    virtual void _set(bool on);
    void begin();
};
