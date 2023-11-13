#ifndef _H_EXP_GPIO
#define _H_EXP_GPIO
#include <Arduino.h>
#include <Wire.h>

#include "Adafruit_MCP23X17.h"

// Use the top 2 bits for marking  the local/extended output.
//
#define	PIN_GPIO_MASK  (3 << 6)
#define PIN_HPIO_PLAIN (0 << 6)
#define PIN_HPIO_MCP   (1 << 6)
#define PIN_HPIO_RES3  (2 << 6)
#define PIN_HPIO_RES4  (3 << 6)

void expandedPinMode(uint8_t pin, uint8_t mode);
int expandedDigitaRead(uint8_t pin);
void expandedDigitalWrite(uint8_t pin, uint8_t val);

class ExpandedPGIO {
	public:
		ExpandedPGIO();
		~ExpandedPGIO();
		void begin(TwoWire * wire = &Wire, unsigned int mcp23addr = 0);

	// Easiest to be public - will be used from plain C
	Adafruit_MCP23X17 * mcp = NULL;
};
#endif
