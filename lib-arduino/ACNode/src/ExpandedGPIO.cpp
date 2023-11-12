#include "ExpandedGPIO.h"
#include <TLog.h>

static ExpandedPGIO * __exp = NULL;

void ExpandedPGIO::begin(TwoWire * wire, unsigned int mcp23addr) {
	if (mcp23addr) {
		mcp = new Adafruit_MCP23X17();
		mcp->begin_I2C(mcp23addr,wire);
	};
}

ExpandedPGIO::~ExpandedPGIO() {
	if (!__exp) return;

	if (mcp) delete mcp;

	delete __exp;
	__exp = NULL;
	Log.printf("ExpandedGPIO destroyed -- did you really expect that");
}

void expandedPinMode(uint8_t pin, uint8_t mode) {
	if ((pin & PIN_GPIO_MASK) == PIN_HPIO_PLAIN) {
		pinMode(pin,mode);
		return;
	};
	if (((pin & PIN_GPIO_MASK) == PIN_HPIO_MCP) && __exp && __exp->mcp) {
		__exp->mcp->pinMode(pin & ~PIN_GPIO_MASK, mode);
		return;
	};

	Log.printf("No expanded pinMode() for pin 0x%x, ignored.", pin);
}


int expandedDigitaRead(uint8_t pin) {
	if ((pin & PIN_GPIO_MASK) == PIN_HPIO_PLAIN)
		return digitalRead(pin);

	if (((pin & PIN_GPIO_MASK) == PIN_HPIO_MCP) && __exp && __exp->mcp) 
		return __exp->mcp->digitalRead(pin & ~PIN_GPIO_MASK) ? HIGH : LOW;

	Log.printf("No expanded digitalRead() for pin 0x%x, ignored.", pin);
	return -1;
}


void expandedDigitalWrite(uint8_t pin, uint8_t val) {
	if ((pin & PIN_GPIO_MASK) == PIN_HPIO_PLAIN) {
		digitalWrite(pin,val);
		return;
	};
	if (((pin & PIN_GPIO_MASK) == PIN_HPIO_MCP) && __exp && __exp->mcp) {
		__exp->mcp->digitalWrite(pin & ~PIN_GPIO_MASK, val);
		return;
	};

	Log.printf("No expanded digitalWrite() for pin 0x%x, ignored.", pin);
}
