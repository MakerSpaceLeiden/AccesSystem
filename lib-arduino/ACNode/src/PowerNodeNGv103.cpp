#include "PowerNodeNGv103.h"

static void setup_i2c_power_ctrl() {
  // for recovery switch I2C
  pinMode(GPIOPORT_I2C_RECOVER_SWITCH, OUTPUT);
  digitalWrite(GPIOPORT_I2C_RECOVER_SWITCH, 0);
}

#if 0
void resetNFCReader() {
  if (USE_NFC_RFID_CARD) {
    pinMode(RFID_SCL_PIN, OUTPUT);
    digitalWrite(RFID_SCL_PIN, 0);
    pinMode(RFID_SDA_PIN, OUTPUT);
    digitalWrite(RFID_SDA_PIN, 0);
    digitalWrite(GPIOPORT_I2C_RECOVER_SWITCH, 1);
    delay(500);
    digitalWrite(GPIOPORT_I2C_RECOVER_SWITCH, 0);
    reader.begin();
  }
}

void checkNFCReaderAvailable(bool onlyShowError) {
  if (USE_NFC_RFID_CARD) {
    if (!reader.CheckPN53xBoardAvailable()) {
      // Error in communuication with RFID reader, try resetting communication
      Serial.println("Error in communication with RFID reader. Resetting communication\r");
      pinMode(RFID_SCL_PIN, OUTPUT);
      digitalWrite(RFID_SCL_PIN, 0);
      pinMode(RFID_SDA_PIN, OUTPUT);
      digitalWrite(RFID_SDA_PIN, 0);
      digitalWrite(GPIOPORT_I2C_RECOVER_SWITCH, 1);
      delay(500);
      digitalWrite(GPIOPORT_I2C_RECOVER_SWITCH, 0);
      reader.begin();
    } else {
      // No error
      if (!onlyShowError) {
        Serial.println("Reader is available!\r");
      }
    }
  }
}

void setupPowernodeNG() {
   setup_i2c_power_ctrl(GPIO);
   setup_MCP23017();
   relay1Off();
}
#endif

void PowerNodeNGv103::begin() {
 	setup_i2c_power_ctrl();

	_reader = new RFID_PN532_NFC();
	_reader->set_debug(false);
        addHandler(_reader);

	ACNode::begin();
}


