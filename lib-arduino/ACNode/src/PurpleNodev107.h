#ifndef _H_PURPLE107
#define _H_PURPLE107

// https://wiki.makerspaceleiden.nl/mediawiki/index.php/Powernode_1.1
//
#include <RFID_MFRC522.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

// Purple - 1.07
const uint8_t LED_INDICATOR = 0;
const uint8_t OUT0 = 16;
const uint8_t OUT1 = 04;
const uint8_t BUTT0 = 14;
const uint8_t BUTT1 = 13;
const uint8_t OPTO0 = 34;
const uint8_t OPTO1 = 35;
const uint8_t CURR0 = 36; // SENSOR_VN
const uint8_t CURR1 = 37; // SENSOR_VP
const uint8_t BUZZER = 2;

const uint8_t RFID_ADDR = 0x28;
const uint8_t RFID_RESET = 32;
const uint8_t RFID_IRQ = 33;


const uint8_t I2C_SDA = 05; // 21 is the default
const uint8_t I2C_SCL = 15; // 22 is the default

// Oled desplay - type SH1106G via i2c. Not always wired up.
//
const uint8_t i2c_Address = 0x3c;
const uint16_t SCREEN_WIDTH = 128; // OLED display width, in pixels
const uint16_t SCREEN_HEIGHT = 64; // OLED display height, in pixels
const uint8_t OLED_RESET = -1;     //  Not wired up

#include "ACNode.h"

class PurpleNodev107 : public ACNode {
  public: 
	PurpleNodev107(const char * machine = NULL, bool wired = true, acnode_proto_t proto = PROTO_SIG2) 
		: ACNode(machine, wired, proto) {};
	void begin();
  private:
	// reader build into the board - so only one type.
	//
        RFID_MFRC522 * _reader;
};
#endif
