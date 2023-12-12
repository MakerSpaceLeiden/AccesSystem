#ifndef _H_POWERNODENG103
#define _H_POWERNODENG103

#include "ACNode.h"
#include <RFID.h>
#include <RFID_PN532_EX.h>
#include <Wire.h>
#include <ExpandedGPIO.h>

#define PWNG_I2C_SDA	(13)
#define PWNG_I2C_SCL	(16)

#define CHECK_NFC_READER_AVAILABLE_TIME_WINDOW  (10000) // in ms  
#define GPIOPORT_I2C_RECOVER_SWITCH             (15)       

#define PWNG_OPTO_COUPLER_INPUT1                     (32)
#define PWNG_OPTO_COUPLER_INPUT2                     (33)
#define PWNG_OPTO_COUPLER_INPUT3                     (36)

#define PWNG_CURRENT_INPUT1				(35)	// analog input pin.
#define CURRENT_SAMPLES_PER_SEC                 (500)   // one sample each 2 ms
#define CURRENT_THRESHOLD                       (0.15)  // Irms ~ 4A, with current transformer 1:1000 and resistor of 36 ohm

#define BLINK_NOT_CONNECTED                     (1000)// blink on/of every 1000 ms
#define BLINK_ERROR                             (300) // blink on/of every 300 ms
#define BLINK_CHECKING_RFIDCARD                 (600) // blink on/of every 600 ms

#define RFID_RESET_PIN (-1)
#define RFID_IRQ_PIN (-1)

#define PWNG_SWITCH1					( 0 | PIN_HPIO_MCP)		/* GPA0 on MCP23017 */
#define PWNG_SWITCH2					( 1 | PIN_HPIO_MCP)		/* GPA1 on MCP23017 */
#define PWNG_SWITCH3					( 2 | PIN_HPIO_MCP)		/* GPA2 on MCP23017 */

#define PWNG_RELAY1					( 8 | PIN_HPIO_MCP) 		/* GPB0 on MCP23017 */
#define PWNG_RELAY2					( 9 | PIN_HPIO_MCP) 		/* GPB1 on MCP23017 */
#define PWNG_RELAY3					(10 | PIN_HPIO_MCP) 		/* GPB2 on MCP23017 */

#define PWNG_LED					(11 | PIN_HPIO_MCP) 		/* GPB3 on MCP23017 */

#define PWNG_FET1					(11 | PIN_HPIO_MCP)		/* GPA3 on MCP23017 */
#define PWNG_FET2					(12 | PIN_HPIO_MCP)		/* GPA4 on MCP23017 */

#ifndef PN532_CHECK_EVERY_SECONDS
#define PN532_CHECK_EVERY_SECONDS (30)		// Keep alive check every 30 seconds for the reader.
#endif

#ifndef MCP_I2C_ADDR
#define MCP_I2C_ADDR (0x20)
#endif

class PowerNodeNGv103 : public ACNode {
    public:
	PowerNodeNGv103(const char * machine, const char * ssid, const char * ssid_passwd, acnode_proto_t proto = PROTO_SIG2) 
		: ACNode(machine, ssid, ssid_passwd, proto) { pop(); };
	PowerNodeNGv103(const char * machine = NULL, bool wired = true, acnode_proto_t proto = PROTO_SIG2) 
		: ACNode(machine, wired, proto) { pop(); };

	void begin(); 
	void loop(); 
	void onSwipe(RFID::THandlerFunction_SwipeCB fn);
    private:
	RFID_PN532_EX * _reader;
        unsigned long _last_pn532_check = 0, _last_seen_alive = 0;
	void pop();
};
#endif
