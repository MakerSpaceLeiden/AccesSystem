#include "PurpleNodev107.h"

void PurpleNodev107::begin() {
   // Non standard pins for i2c.
   Wire.begin(I2C_SDA, I2C_SCL);

   // All nodes have a build-in RFID reader; so fine to hardcode this.
   //
   _reader = new RFID_MFRC522(&Wire, RFID_ADDR, RFID_RESET, RFID_IRQ);
   addHandler(_reader);

   ACNode::begin(BOARD_OLIMEX);
}
