#include "PowerNodeNGv103.h"
#include <OlimexBoard.h>
#include <ETH.h>

void PowerNodeNGv103::begin() {
	gpio = new ExpandedGPIO();
	gpio->begin(MCP_I2C_ADDR);

  	// for recovery switch of NFC reader
	//
	xpinMode(GPIOPORT_I2C_RECOVER_SWITCH, OUTPUT);
	xdigitalWrite(GPIOPORT_I2C_RECOVER_SWITCH, 0);

	_reader = new RFID_PN532_EX();
	_reader->set_debug(false);
        addHandler(_reader);

        ETH.begin(ETH_PHY_ADDR, -1 /* revision K, NRST is RC */, ETH_PHY_MDC, ETH_PHY_MDIO, ETH_PHY_LAN8720, ETH_CLOCK_GPIO17_OUT);

	ACNode::begin();
}

void PowerNodeNGv103::loop() {
#if 0
	if ((_last_seen_alive) && (millis() - _last_seen_alive > 2500)) {
	        Log.println("ALARM - PN532 has gone south again. Resetting.");

		// Powercycle the NFC reader.
		//
		xdigitalWrite(GPIOPORT_I2C_RECOVER_SWITCH, 1);
		delay(500);
		xdigitalWrite(GPIOPORT_I2C_RECOVER_SWITCH, 0);

        	_reader->begin();
		_last_pn532_check = 0;
	};

   	if (millis() - _last_pn532_check > PN532_CHECK_EVERY_SECONDS * 1000) {
		_last_pn532_check = millis();

	        if (_reader->alive()) {
			Debug.println(_last_seen_alive ? "HURAY - PN532 is back" : "PN532 responding, ought to be ok");
			_last_seen_alive = 0;
		} else {
			_last_seen_alive = millis();
		};
	};
#endif
	ACNode::loop();
};
