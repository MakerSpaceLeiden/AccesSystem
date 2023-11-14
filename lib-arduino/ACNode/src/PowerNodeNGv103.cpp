#include "PowerNodeNGv103.h"

static void setup_i2c_power_ctrl() {
  // for recovery switch of NFC reader
  //
  pinMode(GPIOPORT_I2C_RECOVER_SWITCH, OUTPUT);
  digitalWrite(GPIOPORT_I2C_RECOVER_SWITCH, 0);
}

void resetNFCReader() {
  // Powercycle the NFC reader.
  //
  digitalWrite(GPIOPORT_I2C_RECOVER_SWITCH, 1);
  delay(500);
  digitalWrite(GPIOPORT_I2C_RECOVER_SWITCH, 0);
}

void PowerNodeNGv103::begin() {
 	setup_i2c_power_ctrl();

	_reader = new RFID_PN532_EX();
	_reader->set_debug(false);
        addHandler(_reader);

	ACNode::begin();
}

void PowerNodeNGv103::loop() {
	if ((_last_seen_alive) && (millis() - _last_seen_alive > 2500)) {
	        Log.println("ALARM - PN532 has gone south again. Resetting.");
		resetNFCReader();
        	_reader->begin();
		_last_pn532_check = 0;
	};

   	if (millis() - _last_pn532_check > PN532_CHECK_EVERY_SECONDS * 1000) {
		_last_pn532_check = millis();
		_last_seen_alive = millis();
		
	        if (_reader->alive()) {
			Debug.println(_last_seen_alive ? "HURAY - PN532 is back" ? "PN532 responding, ought to be ok");
			_last_seen_alive = 0;
		};
	};
	ACNode::loop();
};
