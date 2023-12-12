#include "PurpleNodev107.h"

#define ETH_PHY_TYPE        ETH_PHY_LAN8720
#define ETH_PHY_ADDR         0 // PHYAD0 tied to 0
#define ETH_PHY_MDC         23
#define ETH_PHY_MDIO        18
#define ETH_PHY_POWER       12 /* board specific - can be jumpered to always on  */
#define ETH_CLK_MODE        ETH_CLOCK_GPIO17_OUT

#include <ETH.h>
#include <WiredEthernet.h>


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
