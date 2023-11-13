#ifndef _H_POWERNODENG103
#define _H_POWERNODENG103

#include "ACNode.h"
#include <RFID_PN532_NFC.h>

#define CHECK_NFC_READER_AVAILABLE_TIME_WINDOW  (10000) // in ms  
#define GPIOPORT_I2C_RECOVER_SWITCH             (15)       

#define OPTO_COUPLER_INPUT1                     (32)
#define OPTO_COUPLER_INPUT2                     (33)
#define OPTO_COUPLER_INPUT3                     (36)

#define CURRENT_SAMPLES_PER_SEC                 (500) // one sample each 2 ms
#define CURRENT_THRESHOLD                       (0.15)  // Irms ~ 4A, with current transformer 1:1000 and resistor of 36 ohm

#define BLINK_NOT_CONNECTED                     (1000)  // blink on/of every 1000 ms
#define BLINK_ERROR                             (300) // blink on/of every 300 ms
#define BLINK_CHECKING_RFIDCARD                 (600) // blink on/of every 600 ms

#define RFID_RESET_PIN (-1)
#define RFID_IRQ_PIN (-1)

class PowerNodeNGv103 : public ACNode {
    public:
	PowerNodeNGv103(const char * machine, const char * ssid, const char * ssid_passwd, acnode_proto_t proto = PROTO_SIG2) 
		: ACNode(machine, ssid, ssid_passwd, proto) {};
	PowerNodeNGv103(const char * machine = NULL, bool wired = true, acnode_proto_t proto = PROTO_SIG2) 
		: ACNode(machine, wired, proto) {};

	void begin(); 
    private:
	RFID_PN532_NFC * _reader;

};
#endif
