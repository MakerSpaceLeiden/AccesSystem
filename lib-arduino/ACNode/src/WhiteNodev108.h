#ifndef _H_WHITE108
#define _H_WHITE108

// https://wiki.makerspaceleiden.nl/mediawiki/index.php/Powernode_1.1
//
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

// White / 1.08
const uint8_t LED_INDICATOR = 12;
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
const uint8_t SCREEN_Address = 0x3c;
const uint16_t SCREEN_WIDTH = 128; // OLED display width, in pixels
const uint16_t SCREEN_HEIGHT = 64; // OLED display height, in pixels
const uint8_t SCREEN_RESET = -1;     //  Not wired up

#define ETH_PHY_TYPE        ETH_PHY_RTL8201
#define ETH_PHY_ADDR         0 // PHYADxx all tied to 0
#define ETH_PHY_MDC         23
#define ETH_PHY_MDIO        18
#define ETH_CLK_MODE        ETH_CLOCK_GPIO17_OUT

#define ETH_PHY_POWER       -1 // powersafe in software
#define ETH_PHY_RESET	    -1 // wired to EN/esp32 reset

#include <ETH.h>
#include <WiredEthernet.h>
#include <RFID_MFRC522.h>

#include "ACNode.h"

class WhiteNodev108 : public ACNode {
  public: 
	WhiteNodev108(const char * machine = NULL, bool wired = true, acnode_proto_t proto = PROTO_SIG2) 
		: ACNode(machine, wired, proto) {};
	void begin() {
   		// Non standard pins for i2c.
		Wire.begin(I2C_SDA, I2C_SCL);

   		// All nodes have a build-in RFID reader; so fine to hardcode this.
		//
		_reader = new RFID_MFRC522(&Wire, RFID_ADDR, RFID_RESET, RFID_IRQ);
		addHandler(_reader);

		ETH.begin(ETH_PHY_ADDR, ETH_PHY_RESET, ETH_PHY_MDC, ETH_PHY_MDIO, ETH_PHY_RTL8201, ETH_CLOCK_GPIO17_OUT);

		ACNode::begin();
	}
  private:
	// reader build into the board - so only one type; and it is hardcoded.
	//
        RFID_MFRC522 * _reader;
};
#endif
