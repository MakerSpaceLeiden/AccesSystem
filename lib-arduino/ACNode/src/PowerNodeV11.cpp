#include "PowerNodeV11.h"

void PowerNodeV11::begin() {
   xpinMode(RELAY_GPIO, OUTPUT);
   xdigitalWrite(RELAY_GPIO, LOW);

   ACNode::begin();
}
