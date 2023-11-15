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

// Convinience functions that rely on a auto created
// singleton.
extern void expandedPinMode(uint8_t pin, uint8_t mode);
extern int  expandedDigitaRead(uint8_t pin);
extern void expandedDigitalWrite(uint8_t pin, uint8_t val);

class ExpandedGPIO {
    public:
	ExpandedGPIO();
	~ExpandedGPIO();

	void begin(unsigned int mcp23addr, TwoWire * wire = &Wire);

	void xpinMode(uint8_t pin, uint8_t mode);
	int xdigitalRead(uint8_t pin);
	void xdigitalWrite(uint8_t pin, uint8_t val);

	Adafruit_MCP23X17 * mcp = NULL;
};
#endif

