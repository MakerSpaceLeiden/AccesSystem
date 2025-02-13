#include "ExpandedGPIO.h"
#include <TLog.h>
#include <esp_debug_helpers.h>

// Singleton with convenience functions for 'C'.
//
static ExpandedGPIO &__exp = ExpandedGPIO::getInstance();
void expandedPinMode(uint8_t pin, uint8_t mode) { __exp.xpinMode(pin, mode); };
int  expandedDigitalRead(uint8_t pin) { return __exp.xdigitalRead(pin); };
void expandedDigitalWrite(uint8_t pin, uint8_t val) { __exp.xdigitalWrite(pin, val); };
void expandedAnalogWrite(uint8_t pin, uint8_t val) { __exp.xanalogWrite(pin, val); };
unsigned int expandedAnalogRead(uint8_t pin) { return __exp.xanalogRead(pin); };

static int wp = 0;
static const int MAXREPORT=500;

void ExpandedGPIO::addMCP(unsigned int i2caddr, TwoWire * wire) {
    if (mcp) return;
    
    mcp = new Adafruit_MCP23X17();
    mcp->begin_I2C(i2caddr,wire);
}

void ExpandedGPIO::addAW9523(unsigned int i2caddr, TwoWire * wire) {
    if (awp)
        return;
    awp= new Adafruit_AW9523();
    awp->begin(i2caddr,wire);
    awp->reset(); // all pins in open-drain; output mode
    awp->openDrainPort0(false);
}

void ExpandedGPIO::xpinMode(uint8_t pin, uint8_t mode) {
    if ((pin & PIN_GPIO_MASK) == PIN_HPIO_PLAIN) {
        pinMode(pin,mode);
        return;
    } else
        if (((pin & PIN_GPIO_MASK) == PIN_HPIO_MCP) && mcp) {
            mcp->pinMode(pin & ~PIN_GPIO_MASK, mode);
            return;
        } else
            if (((pin & PIN_GPIO_MASK) == PIN_HPIO_AW9523) && awp) {
#if 0
                // Something odd with the init - pinMode does not seem to work.
                //
                aw.reset(); // all pins in open-drain; output mode
                aw.openDrainPort0(false);
                // aw.configureLEDMode((1 << LEDA) | (1 << LEDB) | (1 << LEDC) | (1 << LEDD) | (1 << LEDE));
                aw.configureDirection((1 << LEDA) | (1 << LEDB) | (1 << LEDC) | (1 << LEDD) | (1 << LEDE));
#endif
                awp->pinMode(pin & ~PIN_GPIO_MASK, mode);
                return;
            } else
                if (0) if (wp++<MAXREPORT)
                    Log.printf("No expanded pinMode() for pin 0x%x, ignored.\n", pin);
}


int ExpandedGPIO::xdigitalRead(uint8_t pin) {
    if ((pin & PIN_GPIO_MASK) == PIN_HPIO_PLAIN)
        return digitalRead(pin);
    
    if (((pin & PIN_GPIO_MASK) == PIN_HPIO_MCP) && mcp)
        return mcp->digitalRead(pin & ~PIN_GPIO_MASK) ? HIGH : LOW;
    
    if (((pin & PIN_GPIO_MASK) == PIN_HPIO_AW9523) && awp)
            return awp->digitalRead(pin & ~PIN_GPIO_MASK) ? HIGH : LOW;
    
    if (0) if (wp++<MAXREPORT)
        Log.printf("No expanded digitalRead() for pin 0x%x, ignored.\n", pin);

    return -1;
}

unsigned int ExpandedGPIO::xanalogRead(uint8_t pin) {
    if ((pin & PIN_GPIO_MASK) == PIN_HPIO_PLAIN) 
	return analogRead(pin);
    if (((pin & PIN_GPIO_MASK) == PIN_HPIO_AW9523) && awp) 
	return -1;
    if (((pin & PIN_GPIO_MASK) == PIN_HPIO_AW9523) && awp)
	return -1;

    if (0) if (wp++<MAXREPORT)
        Log.printf("No expanded analogRead() for pin 0x%x, ignored.\n", pin);

    return -1;
}

void ExpandedGPIO::xdigitalWrite(uint8_t pin, uint8_t val) {
#if 0
    // Useful for debugging AW9523 glitches caused by something
    // in AdafruitIO perhaps not doing an endTransmission().
    //
    if (pin == (PIN_HPIO_AW9523 | (8+7)))
        Log.println(val ? "xdigitalWrite(buzzer,ON)" : "xdigitalWrite(buzzer,OFF)");
#endif
    if ((pin & PIN_GPIO_MASK) == PIN_HPIO_PLAIN) {
        digitalWrite(pin,val);
        return;
    } else
        if (((pin & PIN_GPIO_MASK) == PIN_HPIO_AW9523) && awp) {
            awp->digitalWrite(pin & ~PIN_GPIO_MASK, val);
            return;
        } else
            if (0) if (wp++<MAXREPORT)
                Log.printf("No expanded digitalWrite() for pin 0x%x, ignored.\n", pin);
}

void ExpandedGPIO::xanalogWrite(uint8_t pin, uint8_t val) {
    if ((pin & PIN_GPIO_MASK) == PIN_HPIO_PLAIN) {
        analogWrite(pin,val);
        return;
    } else
        if (((pin & PIN_GPIO_MASK) == PIN_HPIO_AW9523) && awp) {
            awp->analogWrite(pin & ~PIN_GPIO_MASK, val);
            return;
        } else
            if (wp++<MAXREPORT)
                if (0) Log.printf("No expanded analogWrite() for pin 0x%x, ignored.\n", pin);
}
