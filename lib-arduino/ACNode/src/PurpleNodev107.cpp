#include "PurpleNodev107.h"

void PurpleNodev107::begin() {
   // Non standard pins for i2c.
   Wire.begin(I2C_SDA, I2C_SCL);

   // All nodes have a build-in RFID reader; so fine to hardcode this.
   //
   _reader = new RFID_MFRC522(&Wire, RFID_ADDR, RFID_RESET, RFID_IRQ);
   addHandler(_reader);

   ETH.begin(ETH_PHY_ADDR, -1, ETH_PHY_MDC, ETH_PHY_MDIO, ETH_PHY_LAN8720, ETH_CLOCK_GPIO17_OUT);

   ACNode::begin(BOARD_NG);
}
