#include "ExpandedGPIO.h"
#include <TLog.h>

// Singleton with convenience functions for 'C'.
//
static ExpandedGPIO &__exp = ExpandedGPIO::getInstance();
void expandedPinMode(uint8_t pin, uint8_t mode) { __exp.xpinMode(pin, mode); };
int  expandedDigitaRead(uint8_t pin) { return __exp.xdigitalRead(pin); };
void expandedDigitalWrite(uint8_t pin, uint8_t val) { __exp.xdigitalWrite(pin, val); };

static int wp = 0;
static const int MAXREPORT=50;

void ExpandedGPIO::addMCP(unsigned int mcp23addr, TwoWire * wire) {
	if (mcp == NULL) {
		mcp = new Adafruit_MCP23X17();
		mcp->begin_I2C(mcp23addr,wire);
	};
}

void ExpandedGPIO::xpinMode(uint8_t pin, uint8_t mode) {
	if ((pin & PIN_GPIO_MASK) == PIN_HPIO_PLAIN) {
		pinMode(pin,mode);
		return;
	};
	if (((pin & PIN_GPIO_MASK) == PIN_HPIO_MCP) && mcp) {
		mcp->pinMode(pin & ~PIN_GPIO_MASK, mode);
		return;
	};

	if (wp++<MAXREPORT) 
	Log.printf("No expanded pinMode() for pin 0x%x, ignored.\n", pin);
}


int ExpandedGPIO::xdigitalRead(uint8_t pin) {
	if ((pin & PIN_GPIO_MASK) == PIN_HPIO_PLAIN)
		return digitalRead(pin);

	if (((pin & PIN_GPIO_MASK) == PIN_HPIO_MCP) && mcp) 
		return mcp->digitalRead(pin & ~PIN_GPIO_MASK) ? HIGH : LOW;

	if (wp++<MAXREPORT) 
	Log.printf("No expanded digitalRead() for pin 0x%x, ignored.\n", pin);
	return -1;
}


void ExpandedGPIO::xdigitalWrite(uint8_t pin, uint8_t val) {
	if ((pin & PIN_GPIO_MASK) == PIN_HPIO_PLAIN) {
		digitalWrite(pin,val);
		return;
	};
	if (((pin & PIN_GPIO_MASK) == PIN_HPIO_MCP) && mcp) {
		mcp->digitalWrite(pin & ~PIN_GPIO_MASK, val);
		return;
	};
	if (wp++<MAXREPORT) 
	Log.printf("No expanded digitalWrite() for pin 0x%x, ignored.\n", pin);
}

