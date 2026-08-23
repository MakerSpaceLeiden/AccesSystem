#ifndef _H_EXP_GPIO
#define _H_EXP_GPIO
#include <Arduino.h>
#include <Wire.h>

#ifdef  _HAS_MCP
#include <Adafruit_MCP23X17.h>
#endif

#include <Adafruit_AW9523.h>

// Use the top 2 bits for marking  the local/extended output.
//
#define	PIN_GPIO_MASK   (3 << 6)

#define PIN_HPIO_PLAIN  (0 << 6)
#ifdef  _HAS_MCP
#define PIN_HPIO_MCP    (1 << 6)
#endif
#define PIN_HPIO_AW9523 (2 << 6)
#define PIN_HPIO_RES4   (3 << 6)	// not yet used.

// Convinience functions that rely on a auto created
// singleton.
//
extern void expandedPinMode(uint8_t pin, uint8_t mode);
extern int  expandedDigitalRead(uint8_t pin);
extern void expandedDigitalWrite(uint8_t pin, uint8_t val);
extern void expandedAnalogWrite(uint8_t pin, uint8_t val);
extern unsigned int expandedAnalogRead(uint8_t pin);

class ExpandedGPIO {
public:
    static ExpandedGPIO& getInstance() {
        static ExpandedGPIO instance;
        return instance;
   }
    //        ExpandedGPIO(ExpandedGPIO const&) = delete;
    //        void operator=(ExpandedGPIO const&) = delete;
    
#ifdef  _HAS_MCP
    void addMCP(unsigned int i2c_addr = 0x20, TwoWire * wire = &Wire);
#endif
    void addAW9523(unsigned int i2c_addr = 0x58, TwoWire * wire = &Wire);
    // void addH2812(unsigned int i2caddr, TwoWire * wire = &Wire);
    
    void xpinMode(uint8_t pin, uint8_t mode);

    int xdigitalRead(uint8_t pin);
    unsigned int xanalogRead(uint8_t pin);

    void xdigitalWrite(uint8_t pin, uint8_t val);
    void xanalogWrite(uint8_t pin, uint8_t val);

    // void setCurrent(uint8_t pin = -1, uint8_t val);

    void debugdump() {
#ifdef  _HAS_MCP
    	if (mcp) {Serial.printf("MCP:"); for (int i = 0; i < 16; i++) { 
		Serial.print(mcp->digitalRead(i));
		    if (i % 4 == 3) Serial.print(".");
  	}; Serial.println(); };
#endif
    	if (awp) {Serial.printf("AWP:"); for (int i = 0; i < 16; i++) { 
		Serial.print(awp->digitalRead(i));
		    if (i % 4 == 3) Serial.print(".");
  	}; Serial.println(); };
    };
private:
    ExpandedGPIO() {};
    ~ExpandedGPIO() { Serial.println("DESTROY ExpandedGPIO should not happen"); };
    ExpandedGPIO(ExpandedGPIO const&);              
    void operator=(ExpandedGPIO const&);

#ifdef  _HAS_MCP
    Adafruit_MCP23X17 * mcp = NULL;
#endif
    Adafruit_AW9523 * awp = NULL;
};
#endif

