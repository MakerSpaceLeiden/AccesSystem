#include "PowerNodeNGv103.h"

static void setup_i2c_power_ctrl() {
  // for recovery switch I2C
  pinMode(GPIOPORT_I2C_RECOVER_SWITCH, OUTPUT);
  digitalWrite(GPIOPORT_I2C_RECOVER_SWITCH, 0);
}

void resetNFCReader() {
  pinMode(RFID_SCL_PIN, OUTPUT);
  digitalWrite(RFID_SCL_PIN, 0);
  pinMode(RFID_SDA_PIN, OUTPUT);
  digitalWrite(RFID_SDA_PIN, 0);

  digitalWrite(GPIOPORT_I2C_RECOVER_SWITCH, 1);
  delay(500);
  digitalWrite(GPIOPORT_I2C_RECOVER_SWITCH, 0);
}

#if 0
void setupPowernodeNG() {
   setup_i2c_power_ctrl(GPIO);
   setup_MCP23017();
   relay1Off();
}
#endif

void PowerNodeNGv103::begin() {
 	setup_i2c_power_ctrl();

	_reader = new RFID_PN532_EX();
	_reader->set_debug(false);
        addHandler(_reader);

	ACNode::begin();
}

void PowerNodeNGv103::loop() {
   	if (millis() - _last_pn532_check > PN532_CHECK_EVERY_SECONDS * 1000) {
		_last_pn532_check = millis();
		
	        if (_reader->alive()) {
			Debug.println("PN532 still responding, ought to be ok");
			return;
		};

	        Log.println("ALARM - PN532 has gone south again. Resetting.");
		resetNFCReader();

        	_reader->begin();
	};

	ACNode::loop();
};
