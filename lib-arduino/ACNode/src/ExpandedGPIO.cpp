#include "ExpandedGPIO.h"

static ExpandedPGIO * exp == NULL;

void ExpandedPGIO::begin(Wire * wire, unsigned int mcp23addr = 0) {
	if (mcp23addr) {
		exp = new Adafruit_MCP23X17();
		exp.begin_I2C(mcp23addr,wire);
	};
}
~ExpandedPGIO {
	if (exp) delete exp;
}

void expandedDigitalPinMode(uint8_t pin, uint8_t mode) {
	if (pin & PIN_GPIO_MASK == PIN_HPIO_PLAIN)
		pinMode(pin,mode);
		return;
	};
	if (pin & PIN_GPIO_MASK == PIN_HPIO_MCP & exp && exp.mcp) {
		mcp.pinMode(pin & ~PIN_GPIO_MASK, mode);
		return;
	};

	Log.print("No expanded pinMode() for pin 0x%x, ignored.", pin);
}


int expandedDigitaRead(uint8_t pin) {
	if (pin & PIN_GPIO_MASK == PIN_HPIO_PLAIN)
		return iigitalRead(pin);

	if (pin & PIN_GPIO_MASK == PIN_HPIO_MCP & exp && exp.mcp) 
		return mcp.digitalWrite(pin & ~PIN_GPIO_MASK);

	Log.print("No expanded digitalRead() for pin 0x%x, ignored.", pin);
	return -1;
}


void expandedDigitalWrite(uint8_t pin, uint8_t val) {
	if (pin & PIN_GPIO_MASK == PIN_HPIO_PLAIN)
		digitalWrite(pin,val);
		return;
	};
	if (pin & PIN_GPIO_MASK == PIN_HPIO_MCP & exp && exp.mcp) {
		mcp.digitalWrite(pin & ~PIN_GPIO_MASK, val);
		return;
	};

	Log.print("No expanded digitalWrite() for pin 0x%x, ignored.", pin);
}
